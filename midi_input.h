#ifndef MIDI_INPUT_H
#define MIDI_INPUT_H

#include "event_buffer.h"
#include "midi_common.h"
typedef struct {
	event_buffer** buffers;
	uint16_t num_buffers;
} miditrack_connections;


typedef struct _miditrack {
	FILE* stream;

	int64_t bytes_remaining;
	uint16_t sequence_number;
	char* name;

	uint8_t running_status;

	uint64_t last_timestamp; //this is in samples the timestamp of the last event from this track (technically this could be a tempo change from another track in the same file)
	float last_timestamp_frac;//this is the fractional part of the above timestamp.

	uint32_t delta_ticks; // this is the number of ticks since last_timestamp of the next event from this track

	float current_samples_per_tick;

	miditrack_connections connections; //These are the event_buffers we want the events from this track to be fed into.
} miditrack;

//a file has an internal tree to merge all the events from the various tracks.
typedef struct _miditracknode {
	struct _miditracknode* lchild;
	struct _miditracknode* rchild;
	miditrack t;
} miditracknode;

typedef struct _midifile {
	uint16_t format;
	uint16_t numtracks;
	uint16_t ppqn; //This is the translated value of "division" in the midi file header to pulses per quarter note

	//Array of all the midi tracks data structures.
	miditracknode * tracks;
	//Pointer to the head of the tree of tracks.
	miditracknode* trackshead;

	uint64_t next_timestamp;

	double sample_rate;
} midifile;

midi_error_code open_midi_file(const char* filename, midifile* header,double samplerate);
void miditrack_connections_add_connection(miditrack_connections* c, event_buffer* b);
midi_error_code transfer_events_before_timestamp(midifile* f, uint64_t timestamp);
midi_error_code close_midi_input_file(midifile* file);

#endif
