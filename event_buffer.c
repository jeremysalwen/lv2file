#include "event_buffer.h"
#include <stdlib.h>

#include <string.h>

#include <lv2/lv2plug.in/ns/ext/midi/midi.h>

//Our refs are offset by one, as required by the forge API
LV2_Atom_Forge_Ref event_buffer_sink(LV2_Atom_Forge_Sink_Handle handle, const void* buf, uint32_t size) {
	event_buffer* b=(event_buffer*)handle;
	if(b->write_index+size>b->allocated_size) {
		while(b->write_index+size>b->allocated_size) {
			b->allocated_size*=2;
		}
		b->buffer=realloc(b->buffer,b->allocated_size);
	}
	memmove(b->buffer+b->write_index,buf,size);
	b->write_index+=size;
	return b->write_index+1;
}

LV2_Atom*  event_buffer_deref(LV2_Atom_Forge_Sink_Handle handle, LV2_Atom_Forge_Ref ref) {
	return (LV2_Atom*)(((event_buffer*)handle)->buffer+ref-1);
}

void eb_event_start(event_buffer* b, uint64_t timestamp) {
	lv2_atom_forge_frame_time(&b->forge, timestamp-b->start_timestamp);
	lv2_atom_forge_push(
		&b->forge,
		&b->frame,
		lv2_atom_forge_atom(&b->forge,0,b->midi_event_type));
}

void eb_event_write(event_buffer* b,uint8_t byte) {
	lv2_atom_forge_raw(&b->forge,&byte,sizeof(byte));
}

void eb_event_end(event_buffer* b) {
	lv2_atom_forge_pop(&b->forge,&b->frame);
}

void clear_event_buffer(event_buffer* b,uint64_t start_timestamp) {
	b->start_timestamp=start_timestamp;
	b->write_index=0;
	lv2_atom_forge_set_sink(&b->forge,event_buffer_sink,event_buffer_deref,b); //Clears the stack
	LV2_Atom_Forge_Frame frame; //We don't need to save this, because this sequence is the *only* thing we are writing to the buffer.
				    //So we don't ever pop it!
	lv2_atom_forge_sequence_head(&b->forge,&frame,0); //The unit is 0, because it is implicitly assumed to be samples.  
}

//From jalv by David Robillard
static LV2_URID map_uri(LV2_URID_Map_Handle handle,  const char*  uri)
{
	return symap_map((Symap*)handle, uri);
}

void init_event_buffer(event_buffer* b, Symap* urimap, uint32_t buffer_size) {
	b->allocated_size=buffer_size;
	b->buffer=malloc(b->allocated_size);
	b->write_index=0;

	b->midi_event_type=symap_map(urimap,LV2_MIDI_URI);
	LV2_URID_Map map={urimap,&map_uri};
	lv2_atom_forge_init(&b->forge,&map);
	
	clear_event_buffer(b,0);
}


void destroy_event_buffer(event_buffer* b) {
	free(b->buffer);
}

