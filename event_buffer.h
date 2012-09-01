#ifndef EVENT_BUFFER_H
#define EVENT_BUFFER_H

#include <lv2/lv2plug.in/ns/ext/atom/forge.h>
#include "symap.h"

typedef struct _event_buffer {
	void* buffer;
	uint64_t allocated_size;
	uint64_t write_index;

	uint64_t start_timestamp;

	LV2_URID midi_event_type;
	LV2_Atom_Forge forge;
	LV2_Atom_Forge_Frame frame;
} event_buffer;

void eb_event_start(event_buffer* b, uint64_t timestamp);
void eb_event_write(event_buffer* b,uint8_t byte);
void eb_event_end(event_buffer* b);

void clear_event_buffer(event_buffer* b,uint64_t start_timestamp);

void init_event_buffer(event_buffer* b, Symap* urimap,uint32_t buffer_size);

void destroy_expandable_buffer(event_buffer* b);


#endif
