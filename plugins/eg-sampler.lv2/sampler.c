/*
  LV2 Sampler Example Plugin
  Copyright 2011-2012 David Robillard <d@drobilla.net>
  Copyright 2011 Gabriel M. Beddingfield <gabriel@teuton.org>
  Copyright 2011 James Morris <jwm.art.net@gmail.com>

  Permission to use, copy, modify, and/or distribute this software for any
  purpose with or without fee is hereby granted, provided that the above
  copyright notice and this permission notice appear in all copies.

  THIS SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
  WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
  MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
  ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
  WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
  ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
  OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/

/**
   @file sampler.c Sampler Plugin

   A simple example of an LV2 sampler that dynamically loads samples (based on
   incoming events) and also triggers their playback (based on incoming MIDI
   note events).  The sample must be monophonic.

   So that the run() method stays real-time safe, the plugin creates a worker
   thread (worker_thread_main) that listens for file loading events.  It loads
   everything in plugin->pending_samp and then signals the run() that it's time
   to install it.  run() just has to swap pointers... so the change happens
   very fast and atomically.
*/

#include <assert.h>
#include <math.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include <sndfile.h>

#include "lv2/lv2plug.in/ns/ext/atom/atom-helpers.h"
#include "lv2/lv2plug.in/ns/ext/atom/forge.h"
#include "lv2/lv2plug.in/ns/ext/message/message.h"
#include "lv2/lv2plug.in/ns/ext/state/state.h"
#include "lv2/lv2plug.in/ns/ext/urid/urid.h"
#include "lv2/lv2plug.in/ns/lv2core/lv2.h"

#include "zix/sem.h"
#include "zix/thread.h"
#include "zix/ring.h"

#include "./uris.h"

#define RING_SIZE 4096

enum {
	SAMPLER_CONTROL  = 0,
	SAMPLER_RESPONSE = 1,
	SAMPLER_OUT      = 2
};

static const char* default_sample_file = "monosample.wav";

typedef struct {
	SF_INFO info;      /**< Info about sample from sndfile */
	float*  data;      /**< Sample data in float */
	char*   path;      /**< Path of file */
	size_t  path_len;  /**< Length of path */
} Sample;

typedef struct {
	/* Features */
	LV2_URID_Map* map;

	/* Forge for creating atoms */
	LV2_Atom_Forge forge;

	/* Worker thread, communication, and sync */
	ZixThread worker_thread;
	ZixSem    signal;
	ZixRing*  to_worker;
	ZixRing*  from_worker;
	bool      exit;

	/* Sample */
	Sample* sample;

	/* Ports */
	float*                output_port;
	LV2_Atom_Port_Buffer* control_port;
	LV2_Atom_Port_Buffer* notify_port;

	/* URIs */
	SamplerURIs uris;

	/* Playback state */
	sf_count_t frame;
	bool       play;
} Sampler;

/** An atom-like message used internally to apply/free samples.
 *
 * This is only used internally via ringbuffers, since it is not POD and
 * therefore not strictly an Atom.
 */
typedef struct {
	LV2_Atom atom;
	Sample*  sample;
} SampleMessage;

static Sample*
load_sample(Sampler* plugin, const char* path)
{
	const size_t path_len  = strlen(path);

	printf("Loading sample %s\n", path);
	Sample* const  sample  = (Sample*)malloc(sizeof(Sample));
	SF_INFO* const info    = &sample->info;
	SNDFILE* const sndfile = sf_open(path, SFM_READ, info);

	if (!sndfile || !info->frames || (info->channels != 1)) {
		fprintf(stderr, "failed to open sample '%s'.\n", path);
		free(sample);
		return NULL;
	}

	/* Read data */
	float* const data = malloc(sizeof(float) * info->frames);
	if (!data) {
		fprintf(stderr, "failed to allocate memory for sample.\n");
		return NULL;
	}
	sf_seek(sndfile, 0ul, SEEK_SET);
	sf_read_float(sndfile, data, info->frames);
	sf_close(sndfile);

	/* Fill sample struct and return it. */
	sample->data     = data;
	sample->path     = (char*)malloc(path_len + 1);
	sample->path_len = path_len;
	memcpy(sample->path, path, path_len + 1);

	return sample;
}

static bool
handle_set_message(Sampler*               plugin,
                   const LV2_Atom_Object* obj)
{
	/* Get file path from message */
	const LV2_Atom* file_path = get_msg_file_path(&plugin->uris, obj);
	if (!file_path) {
		return false;
	}

	/* Load sample. */
	Sample* sample = load_sample(plugin, LV2_ATOM_BODY(file_path));
	if (sample) {
		/* Loaded sample, send it to run() to be applied. */
		const SampleMessage msg = {
			{ plugin->uris.eg_applySample, sizeof(sample) },
			sample
		};
		zix_ring_write(plugin->from_worker,
		               &msg,
		               lv2_atom_pad_size(sizeof(msg)));
	}

	return true;
}

void
free_sample(Sample* sample)
{
	if (sample) {
		fprintf(stderr, "Freeing %s\n", sample->path);
		free(sample->path);
		free(sample->data);
		free(sample);
	}
}

void*
worker_thread_main(void* arg)
{
	Sampler* plugin = (Sampler*)arg;

	while (!zix_sem_wait(&plugin->signal) && !plugin->exit) {
		/* Peek message header to see how much we need to read. */
		LV2_Atom head;
		zix_ring_peek(plugin->to_worker, &head, sizeof(head));

		/* Read message. */
		const uint32_t size = lv2_atom_pad_size(sizeof(LV2_Atom) + head.size);
		uint8_t        buf[size];
		LV2_Atom*      obj  = (LV2_Atom*)buf;
		zix_ring_read(plugin->to_worker, buf, size);

		if (obj->type == plugin->uris.eg_freeSample) {
			/* Free old sample */
			SampleMessage* msg = (SampleMessage*)obj;
			free_sample(msg->sample);
		} else {
			/* Handle set message (load sample). */
			handle_set_message(plugin, (LV2_Atom_Object*)obj);
		}
	}

	return 0;
}

static void
connect_port(LV2_Handle instance,
             uint32_t   port,
             void*      data)
{
	Sampler* plugin = (Sampler*)instance;

	switch (port) {
	case SAMPLER_CONTROL:
		plugin->control_port = (LV2_Atom_Port_Buffer*)data;
		break;
	case SAMPLER_RESPONSE:
		plugin->notify_port = (LV2_Atom_Port_Buffer*)data;
		break;
	case SAMPLER_OUT:
		plugin->output_port = (float*)data;
		break;
	default:
		break;
	}
}

static LV2_Handle
instantiate(const LV2_Descriptor*     descriptor,
            double                    rate,
            const char*               path,
            const LV2_Feature* const* features)
{
	Sampler* plugin = (Sampler*)malloc(sizeof(Sampler));
	if (!plugin) {
		return NULL;
	}

	plugin->sample = (Sample*)malloc(sizeof(Sample));
	if (!plugin->sample) {
		return NULL;
	}

	memset(plugin->sample, 0, sizeof(Sample));
	memset(&plugin->uris, 0, sizeof(plugin->uris));

	/* Scan host features for URID map */
	LV2_URID_Map* map = NULL;
	for (int i = 0; features[i]; ++i) {
		if (!strcmp(features[i]->URI, LV2_URID_URI "#map")) {
			map = (LV2_URID_Map*)features[i]->data;
		}
	}
	if (!map) {
		fprintf(stderr, "Host does not support urid:map.\n");
		goto fail;
	}

	/* Map URIS and initialise forge */
	plugin->map = map;
	map_sampler_uris(plugin->map, &plugin->uris);
	lv2_atom_forge_init(&plugin->forge, plugin->map);

	/* Create signal for waking up worker thread */
	if (zix_sem_init(&plugin->signal, 0)) {
		fprintf(stderr, "Could not initialize semaphore.\n");
		goto fail;
	}

	/* Create worker thread */
	plugin->exit = false;
	if (zix_thread_create(
		    &plugin->worker_thread, 1024, worker_thread_main, plugin)) {
		fprintf(stderr, "Could not initialize worker thread.\n");
		goto fail;
	}

	/* Create ringbuffers for communicating with worker thread */
	plugin->to_worker   = zix_ring_new(RING_SIZE);
	plugin->from_worker = zix_ring_new(RING_SIZE);

	zix_ring_mlock(plugin->to_worker);
	zix_ring_mlock(plugin->from_worker);

	/* Load the default sample file */
	const size_t path_len   = strlen(path);
	const size_t file_len   = strlen(default_sample_file);
	const size_t len        = strlen("file://") + path_len + file_len;
	char*        sample_uri = (char*)malloc(len + 1);
	snprintf(sample_uri, len + 1, "file://%s%s", path, default_sample_file);
	plugin->sample = load_sample(plugin, sample_uri);

	return (LV2_Handle)plugin;

fail:
	free(plugin);
	return 0;
}

static void
cleanup(LV2_Handle instance)
{
	Sampler* plugin = (Sampler*)instance;

	plugin->exit = true;
	zix_sem_post(&plugin->signal);
	zix_thread_join(plugin->worker_thread, 0);
	zix_sem_destroy(&plugin->signal);
	zix_ring_free(plugin->to_worker);
	zix_ring_free(plugin->from_worker);
	free_sample(plugin->sample);
	free(plugin);
}

static void
run(LV2_Handle instance,
    uint32_t   sample_count)
{
	Sampler*     plugin      = (Sampler*)instance;
	SamplerURIs* uris        = &plugin->uris;
	sf_count_t   start_frame = 0;
	sf_count_t   pos         = 0;
	float*       output      = plugin->output_port;

	/* Read incoming events */
	LV2_SEQUENCE_FOREACH((LV2_Atom_Sequence*)plugin->control_port->data, i) {
		LV2_Atom_Event* const ev = lv2_sequence_iter_get(i);
		if (ev->body.type == uris->midi_Event) {
			uint8_t* const data = (uint8_t* const)(ev + 1);
			if ((data[0] & 0xF0) == 0x90) {
				start_frame   = ev->time.audio.frames;
				plugin->frame = 0;
				plugin->play  = true;
			}
		} else if (is_object_type(uris, ev->body.type)) {
			const LV2_Atom_Object* obj = (LV2_Atom_Object*)&ev->body;
			if (obj->type == uris->msg_Set) {
				/* Received a set message, send it to the worker thread. */
				fprintf(stderr, "Queueing set message\n");
				zix_ring_write(plugin->to_worker,
				               obj,
				               lv2_atom_pad_size(
					               lv2_atom_total_size(&obj->atom)));
				zix_sem_post(&plugin->signal);
			} else {
				fprintf(stderr, "Unknown object type %d\n", obj->type);
			}
		} else {
			fprintf(stderr, "Unknown event type %d\n", ev->body.type);
		}
	}

	/* Render the sample (possibly already in progress) */
	if (plugin->play) {
		uint32_t       f  = plugin->frame;
		const uint32_t lf = plugin->sample->info.frames;

		for (pos = 0; pos < start_frame; ++pos) {
			output[pos] = 0;
		}

		for (; pos < sample_count && f < lf; ++pos, ++f) {
			output[pos] = plugin->sample->data[f];
		}

		plugin->frame = f;

		if (f == lf) {
			plugin->play = false;
		}
	}

	/* Add zeros to end if sample not long enough (or not playing) */
	for (; pos < sample_count; ++pos) {
		output[pos] = 0.0f;
	}

	/* Set up forge to write directly to notify output port buffer */
	LV2_Atom* seq = plugin->notify_port->data;
	LV2_Atom_Forge_Frame seq_frame;
	lv2_atom_forge_push(&plugin->forge, &seq_frame, seq);
	lv2_atom_forge_set_buffer(
		&plugin->forge,
		LV2_ATOM_CONTENTS(LV2_Atom_Sequence, seq),
		plugin->notify_port->capacity);

	/* Read messages from worker thread */
	SampleMessage  m;
	const uint32_t msize = lv2_atom_pad_size(sizeof(m));
	while (zix_ring_read(plugin->from_worker, &m, msize) == msize) {
		if (m.atom.type == uris->eg_applySample) {
			/* Send a message to the worker to free the current sample */
			SampleMessage free_msg = {
				{ uris->eg_freeSample, sizeof(plugin->sample) },
				plugin->sample
			};
			zix_ring_write(plugin->to_worker,
			               &free_msg,
			               lv2_atom_pad_size(sizeof(free_msg)));
			zix_sem_post(&plugin->signal);

			/* Install the new sample */
			plugin->sample = m.sample;

			/* Send a notification that we're using a new sample. */
			lv2_atom_forge_audio_time(&plugin->forge, 0, 0);
			write_set_filename_msg(&plugin->forge, uris,
			                       plugin->sample->path,
			                       plugin->sample->path_len);

		} else {
			fprintf(stderr, "Unknown message from worker\n");
		}
	}

	lv2_atom_forge_pop(&plugin->forge, &seq_frame);
}

static void
save(LV2_Handle                instance,
     LV2_State_Store_Function  store,
     void*                     callback_data,
     uint32_t                  flags,
     const LV2_Feature* const* features)
{
	LV2_State_Map_Path* map_path = NULL;
	for (int i = 0; features[i]; ++i) {
		if (!strcmp(features[i]->URI, LV2_STATE__mapPath)) {
			map_path = (LV2_State_Map_Path*)features[i]->data;
		}
	}

	Sampler* plugin = (Sampler*)instance;
	char*    apath  = map_path->abstract_path(map_path->handle,
	                                          plugin->sample->path);

	store(callback_data,
	      plugin->uris.eg_file,
	      apath,
	      strlen(plugin->sample->path) + 1,
	      plugin->uris.atom_Path,
	      LV2_STATE_IS_POD | LV2_STATE_IS_PORTABLE);

	free(apath);
}

static void
restore(LV2_Handle                  instance,
        LV2_State_Retrieve_Function retrieve,
        void*                       callback_data,
        uint32_t                    flags,
        const LV2_Feature* const*   features)
{
	Sampler* plugin = (Sampler*)instance;

	size_t   size;
	uint32_t type;
	uint32_t valflags;

	const void* value = retrieve(
		callback_data,
		plugin->uris.eg_file,
		&size, &type, &valflags);

	if (value) {
		const char* path = (const char*)value;
		printf("Restoring file %s\n", path);
		free_sample(plugin->sample);
		plugin->sample = load_sample(plugin, path);
	}
}

const void*
extension_data(const char* uri)
{
	static const LV2_State_Interface state = { save, restore };
	if (!strcmp(uri, LV2_STATE_URI "#Interface")) {
		return &state;
	}
	return NULL;
}

static const LV2_Descriptor descriptor = {
	SAMPLER_URI,
	instantiate,
	connect_port,
	NULL, // activate,
	run,
	NULL, // deactivate,
	cleanup,
	extension_data
};

LV2_SYMBOL_EXPORT
const LV2_Descriptor* lv2_descriptor(uint32_t index)
{
	switch (index) {
	case 0:
		return &descriptor;
	default:
		return NULL;
	}
}
