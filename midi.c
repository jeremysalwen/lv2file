#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct _miditrack {
	FILE* stream;
	uint64_t head_typestamp;
	uint32_t bytes_remaining;
} miditrack;

typedef struct _midifile {
	uint16_t format;
	uint16_t numtracks;
	uint16_t division;
	miditrack* tracks;
} midifile;

struct _heapnode;

typedef struct _heapnode {
	struct _heapnode* lchild;
	struct _heapnode* rchild;
	miditrack* track;
}

unsigned long ReadVarLen(FILE * midi)
{
    unsigned long value;
    unsigned char c;

    if ( (value = fgetc(midi)) & 0x80 )
    {
       value &= 0x7F;
       do
       {
         value = (value << 7) + ((c = fgetc(midi)) & 0x7F);
       } while (c & 0x80);
    }

    return value;
}

uint32_t readBEint(FILE* midi) {
	uint32_t result=(uint8_t)fgetc(midi);
	result<<=8;
	result|=(uint8_t)fgetc(midi);
	result<<=8;
	result|=(uint8_t)fgetc(midi);
	result<<=8;
	result|=(uint8_t)fgetc(midi);
	return result;
}

uint16_t readBEshort(FILE* midi) {
	uint32_t result=(uint8_t)fgetc(midi);
	result<<=8;
	result|=(uint8_t)fgetc(midi);
	return result;
}

bool readfileheader(FILE* midi, midifile* info) {
	char chunktype[4];
	fread(chunktype,sizeof(char),4,midi);
	if(strncmp(chunktype,"MThd",4)) {
		return false;
	}
	if(readBEint(midi)!=6) {
		return false;
	}
	info->format=readBEshort(midi);
	if(info->format >2) {
		return false;
	}
	info->numtracks=readBEshort(midi);
	if(info->format==0 && info->numtracks != 1) {
		return false;
	}
	info->division=readBEshort(midi);
	return true;
}
bool readtrackheader(FILE* midi, miditrack* info) {
	char chunktype[4];
	fread(chunktype,sizeof(char),4,midi);
	if(strncmp(chunktype,"MTrk",4)) {
		printf("Error reading midi track header\n");
		return false;
	}
	info->stream=midi;
	info->bytes_remaining=readBEint(midi);
	return true;
}
void free_midi_file(midifile* file) {
	free(file->tracks);
}

bool open_midi_file(const char* filename, midifile* header) {
	FILE * f=fopen(filename,"rb");
	if(f==NULL) {
		printf("Error opening file\n");
		return false;
	}
	if(!readfileheader(f,header)) {
		printf("Error reading MIDI header");
		return false;
	}

	miditrack* t=malloc(header->numtracks*sizeof(miditrack));
	if(!readtrackheader(f,&t[0])) {
		return false;
	}
	
	for(int i=1; i<header->numtracks-1; i++) {
		long offset=ftell(f)+t[i-1].bytes_remaining;
		f=fopen(filename,"rb");
		if(f==NULL) {
			printf("Error opening MIDI file multiple times for multiple track\n");
			return false;
		}
		if(fseek(f,offset,SEEK_SET)) {
			printf("Error seeking to MIDI track.");
			return false;
		}
		if(!readtrackheader(f,&t[i])) {
			return false;
		}
	}
	header->tracks=t;
	return true;
}

int main(int argc, char** argv) {
	if(argc!=2) {
		printf("Wrong number of arguments\n");
		return 1;
	}
	midifile header;
	if(!open_midi_file(argv[1],&header)) {
		printf("Error reading MIDI file file\n");
		return 2;
	}
	
	free_midi_file(&header);
	return 0;
}
