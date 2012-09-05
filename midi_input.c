#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdio.h>



void mb_event_write(miditrack_connections* mb, uint8_t byte) {
	for(int i=0; i<mb->num_buffers; i++) {
		eb_event_write(mb->buffers[i],byte);	
	}
}

void mb_event_start(miditrack_connections* mb, uint64_t timestamp) {
	for(int i=0; i<mb->num_buffers; i++) {
		eb_event_start(mb->buffers[i],timestamp);	
	}
}

void mb_event_end(miditrack_connections* mb) {
		for(int i=0; i<mb->num_buffers; i++) {
		eb_event_end(mb->buffers[i]);	
	}
}

void miditrack_connections_add_connection(miditrack_connections* c, event_buffer* b) {
	realloc(c->buffers,c->num_buffers+1);
	c->buffers[c->num_buffers++]=b;
}

static inline char trackgetc(miditrack* t) {
	//printf("trackgetc %p\n",t);
	t->bytes_remaining--;
	return fgetc(t->stream);
}
//From http://home.roadrunner.com/~jgglatt/tech/midifile.htm
uint32_t ReadVarLen(miditrack* midi)
{
    uint32_t value;
    unsigned char c;

    if ( (value = trackgetc(midi)) & 0x80 )
    {
       value &= 0x7F;
       do
       {
         value = (value << 7) + ((c = trackgetc(midi)) & 0x7F);
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
	uint16_t result=(uint8_t)fgetc(midi);
	result<<=8;
	result|=(uint8_t)fgetc(midi);
	return result;
}

static inline void trackseek(miditrack* t, int offset) {
	t->bytes_remaining-=offset;
	fseek(t->stream,offset,SEEK_CUR);
}

static inline void add_ticks_to_timestamp(miditrack* t, uint32_t ticks) {
	t->last_timestamp_frac+=ticks*t->current_samples_per_tick;
	float inc;
	t->last_timestamp_frac=modff(t->last_timestamp_frac,&inc);
	t->last_timestamp+=(uint64_t)inc;
}
midi_input_error handle_meta_event(midifile* file, miditrack* track) {
	uint8_t type=trackgetc(track);
	uint32_t length=ReadVarLen(track);
	printf("Meta event: type 0x%02x, length %d\n",type,length);
	switch(type) {
		case 0x02: //Sequence number event
			track->sequence_number=readBEshort(track->stream);
			track->bytes_remaining-=2;
			goto check_ferror;
		case 0x03: //track name event
			free(track->name);
			char* name=malloc(length+1);
			track->name=name;
			track->bytes_remaining-=length;
			fread(name,1,length,track->stream); //Read in the name
			name[length]=0; //null terminate
			goto check_ferror;
		case 0x2F: //End of track event
			if(track->bytes_remaining!=0) {//We must have precisely finished the track
				return MIDI_ERR_EARLY_END_OF_TRACK;
			} else {
				goto check_ferror;
			} 
		case 0x51:
			if(length!=3) {
				return MIDI_ERR_MALFORMED_TEMPO;
			}
			uint32_t mspqn; //The 24 bit "microseconds per quarter note" value
			{
				mspqn=trackgetc(track);
				mspqn<<=8;
				mspqn|=trackgetc(track);
				mspqn<<=8;
				mspqn|=trackgetc(track);
			}
			printf("NEW TEMPO in Microseconds per Quarter note %d\n",mspqn);
			//Samples/Tick=Samples/Sec * Sec/ms * ms/qn * qn/tick 
			float samples_per_tick=file->sample_rate   * mspqn / (1000000*file->ppqn);
                        if(file->format==1) {  //If file format is "1", then tempo changes affect all tracks
                                for(int i=0; i<file->numtracks; i++) {
					add_ticks_to_timestamp(&file->tracks[i].t,track->delta_ticks);
					file->tracks[i].t.delta_ticks-=track->delta_ticks;
                                        file->tracks[i].t.current_samples_per_tick=samples_per_tick;
                                }
                        } else {//otherwise just affect the current track
                                track->current_samples_per_tick=samples_per_tick;
                        }
                  	goto check_ferror;
                default:
                        trackseek(track,length);
			printf("Skipped unhandled meta-event\n");
                        goto check_ferror;
	}
	check_ferror:
		if(ferror(track->stream)) {
			return MIDI_ERR_READING_EVENT;
		} else {
			return MIDI_ERR_NO_ERROR;
		}
}
midi_error_code handle_midi_event(midifile* file, miditrack* track) {
	uint8_t status=trackgetc(track);
	if(status&0x80) { //Is the first bit set (i.e. it is a status byte)
		track->running_status=status;
	} else {  //Or are we using running status?
		status=track->running_status;
		trackseek(track,-1); //Move back before the byte we jumped over
	}
	printf("New event, status == %x\n",status);
	if(status ==0xFF) { //Meta-event
		return handle_meta_event(file,track);
	}
	mb_event_start(&track->connections,file->next_timestamp); //file->next_timestamp has the cached timestamp of the *current* event.
	if((status &0xF0) == 0xF0) {
		printf("Special event");
		if(status==0xF0) {//System exclusive event
			printf("Sysex event");
			mb_event_write(&track->connections,status);
			uint8_t last_byte=trackgetc(track);
			while(last_byte!=0xF7) { //End of sysex byte
				mb_event_write(&track->connections,last_byte);
				last_byte=trackgetc(track);
			}
			mb_event_write(&track->connections,last_byte);
		} else {
			mb_event_write(&track->connections,status);
			if((status & 0xFE) == 0xF2) { // ==F2 or ==F3, the only multi-byte messages in this branch
				mb_event_write(&track->connections,trackgetc(track));
				if(status==0xF2) {
					mb_event_write(&track->connections,trackgetc(track));
				}
			}
		}
	} else {
		mb_event_write(&track->connections,status);
		mb_event_write(&track->connections,trackgetc(track));
		if((status&0x70) != 0xC0) { //If it's 0xC or 0xD, we only put out one byte
			mb_event_write(&track->connections,trackgetc(track));
		}			
	}
	mb_event_end(&track->connections);
	if(ferror(track->stream)) {
		return MIDI_ERR_READING_EVENT;
	} else {
		return MIDI_ERR_NO_ERROR;
	}
}
//inline 
uint64_t calc_timestamp(miditrack* t) {
	//printf("Calculating timestamp: Last %ld frac %f ticks %d samplesptick %f\n",t->last_timestamp,t->last_timestamp_frac,t->delta_ticks,t->current_samples_per_tick);
	return t->last_timestamp+((uint64_t)(0.5d+t->last_timestamp_frac+t->delta_ticks*t->current_samples_per_tick));
}

//inline 
void cache_next_timestamp(midifile* f) {
	if(f->trackshead==NULL) {
		f->next_timestamp=UINT64_MAX;
	} else {
		miditrack* t=&f->trackshead->t;
		f->next_timestamp=calc_timestamp(t);
	}
}


static inline bool munch_delta_t(miditrack* t) {  //Returns true if there was another event
	if(t->bytes_remaining<=0) {
		return false;
	}
	add_ticks_to_timestamp(t,t->delta_ticks);
	t->delta_ticks=ReadVarLen(t);
	return true;
}

float compare_track_timestamps(const miditracknode* track1, const miditracknode* track2) {  //returns track1stamp-track2stamp
	if(track1==NULL) {
		return 1;
	}
	if(track2==NULL) {
		return -1;
	}
	const miditrack* t1=&track1->t;
	const miditrack* t2=&track2->t;
	int64_t diffi=t1->last_timestamp-t2->last_timestamp;
	float diff=diffi+t1->last_timestamp_frac-t2->last_timestamp_frac;
	diff+=t1->delta_ticks*t1->current_samples_per_tick-t2->delta_ticks*t2->current_samples_per_tick;
	return diff;
}
static inline void sort_2_tracks(miditracknode* tree1, miditracknode* tree2, miditracknode** sooner, miditracknode** later) {
	if(compare_track_timestamps(tree1,tree2)<0) {
		*sooner=tree1;
		*later=tree2;
	} else {
		*sooner=tree2;
		*later=tree1;
	}
}

void merge_trees(miditracknode** hole, miditracknode* tree1, miditracknode* tree2) {
	miditracknode* sooner;
	miditracknode* later;
	sort_2_tracks(tree1,tree2,&sooner,&later);
	if(later==NULL) {
		*hole=sooner;
		return;
	}
	miditracknode* lchild=sooner->lchild;
	miditracknode* rchild=sooner->rchild;
	*hole=sooner;
	sooner->lchild=later;
	merge_trees(&sooner->rchild,lchild,rchild);	
}
//Reestablishes the heap, assuming only the head is out of place
void reestablish_tree(miditracknode** head) {
	miditracknode* h=*head;
	miditracknode* sooner;
	miditracknode* later;
	sort_2_tracks(h->lchild,h->rchild,&sooner,&later);
	if(compare_track_timestamps(h,sooner)<0) { //If the head is the next element, then we don't need to modify the tree.
		return;
	} else {//otherwise...
		*head=sooner; //we move the sooner element to the front, and move the old head to its spot.
		h->lchild=sooner->lchild; 
		h->rchild=sooner->rchild;
		sooner->lchild=later;
		sooner->rchild=h;

		reestablish_tree(&sooner->rchild); //And recurse on the modified subtree.
	}
}
midi_error_code pump_queue(miditracknode** head) {  //Returns the timestamp of the next event
	miditrack* t= &(*head)->t;
	if(munch_delta_t(t)) { //If there are more bytes to this track we update to the latest event and place it properly in the queue
		reestablish_tree(head);
	} else {//Otherwise we just delete this node
		if(t->bytes_remaining!=0) {
			return MIDI_ERR_MISALIGNED_TRACK;
		}
		merge_trees(head,(*head)->lchild,(*head)->rchild);
	}
	return MIDI_ERR_NO_ERROR;
}
void print_tree(midifile* f) {
	printf("Tree head: %ld\n",f->trackshead-f->tracks);
	for(int i=0; i<f->numtracks; i++) {
		printf("Node %d (%f) lchild %ld, rchild %ld\n",i,calc_timestamp(&f->tracks[i].t)/f->sample_rate,f->tracks[i].lchild - f->tracks,f->tracks[i].rchild - f->tracks);
	}
}
midi_error_code transfer_events_before_timestamp(midifile* f, uint64_t timestamp) {
	printf("Next timestamp %ld\n",f->next_timestamp);
	while(f->next_timestamp <timestamp) {
		// print_tree(f);
		if(int errn=handle_midi_event(f,&f->trackshead->t)) {
			return errn;
		}
		if(int errn=pump_queue(&f->trackshead)) {
			return errn;
		}
		cache_next_timestamp(f);
		printf("Next timestamp %f, before %f\n",f->next_timestamp/f->sample_rate,timestamp/f->sample_rate);
	}
	return MIDI_ERR_NO_ERROR;
}

static inline uint16_t division_to_ppqn(int16_t division) {
	if(division<0) {
		int8_t fps=(division&0xFF00)>>8;
		uint8_t subdiv=division&0x00FF;
		return -fps*subdiv;
	} else {
		return division;
	}
}

midi_error_code readfileheader(FILE* midi, midifile* info) {
	char chunktype[4];
	fread(chunktype,sizeof(char),4,midi);
	if(strncmp(chunktype,"MThd",4)) {
		return MIDI_ERR_CORRUPTED_FILE_HEADER;
	}
	if(readBEint(midi)!=6) {
		return MIDI_ERR_CORRUPTED_FILE_HEADER;
	}
	info->format=readBEshort(midi);
	if(info->format >2) {
		return MIDI_ERR_CORRUPTED_FILE_HEADER;
	}
	info->numtracks=readBEshort(midi);
	if(info->format==0 && info->numtracks != 1) {
		return MIDI_ERR_CORRUPTED_FILE_HEADER;
	}
	info->ppqn=division_to_ppqn(readBEshort(midi));
	return MIDI_ERR_NO_ERROR;
}
midi_error_code readtrackheader(midifile* f,FILE* stream, miditrack* info) {
	char chunktype[4];
	fread(chunktype,sizeof(char),4,stream);
	if(strncmp(chunktype,"MTrk",4)) {
		return MIDI_ERR_CORRUPTED_TRACK_HEADER;
	}
	info->name=NULL;
	info->running_status=0;
	info->last_timestamp=0;
	info->last_timestamp_frac=0.0f;
	info->delta_ticks=0;
	info->stream=stream;
	info->connections.buffers=NULL;
	info->connections.num_buffers=0;

	info->bytes_remaining=readBEint(stream);

	info->current_samples_per_tick=f->sample_rate/(2l*f->ppqn);//Default BPM is 120, so two beats/second.
		
	//printf("DEBUG: Track says %ld bytes remaining\n",info->bytes_remaining);

	return MIDI_ERR_NO_ERROR;
}
midi_error_code close_midi_input_file(midifile* file) {
	bool failed=false;
	if(file->tracks) {
		for(int i=0; i<file->numtracks; i++) {
			free(file->tracks[i].t.name);
			if(file->tracks[i].t.stream && fclose(file->tracks[i].t.stream))
				failed=true;
		}
		free(file->tracks);
	}
	if(failed) {
		return MIDI_ERR_CLOSE_FILE;
	} else {
		return MIDI_ERR_NO_ERROR;
	}
}

static int track_comparator(const void* track1, const void* track2) {
	const miditracknode* t1=track1;
	const miditracknode* t2=track2;
	float diff=compare_track_timestamps(t1,t2);
	return (diff>0)-(diff<0);
}
void build_tree(midifile* file) {
	qsort(file->tracks,file->numtracks,sizeof(miditracknode),&track_comparator); //Sort tracks by soon-ness
	file->trackshead=file->tracks;//The head is sorted to the first slot

	unsigned int treesize=file->numtracks;
	const miditracknode* end=&file->tracks[treesize-1];
	while(end->t.last_timestamp==INT64_MAX) { //Figure out how many empty tracks there are at the end.
		end--;
		treesize--;
	}

	for(unsigned int i=0; i<treesize; i++) { //Connect the nodes of the sorted array to form a heap
		unsigned int lchild=i*2+1;
		unsigned int rchild=i*2+2;
		if(lchild>=treesize) {
			file->tracks[i].lchild=NULL;
			file->tracks[i].rchild=NULL;
		} else  {
			file->tracks[i].lchild=file->tracks+lchild;
			if(rchild>=treesize) {
				file->tracks[i].rchild=NULL;
			} else {
				file->tracks[i].rchild=file->tracks+rchild;
			}
		}
	}
	
}
midi_error_code open_midi_file(const char* filename, midifile* header,double samplerate) {
	t=NULL;//Initialize with null, so it will be safe to free, no matter what error occurs on construction.
	FILE * f=fopen(filename,"rb");
	if(f==NULL) {
		return MIDI_ERR_OPEN_FILE;
	}
	if((int errn=readfileheader(f,header))) {
		return errn;
	}
        header->sample_rate=samplerate;
	miditracknode* t=calloc(header->numtracks,sizeof(miditracknode)); //Use calloc so that pointers inside will be NULL-initialized, making it safe to free even if an error occurs on initialization
	if(!t) {
		return MIDI_ERR_OUT_OF_MEMORY;
	}
	t[0].t.sequence_number=0;
	if((int errn=readtrackheader(header,f,&t[0].t))) {
		return errn;
	
	for(int i=1; i<header->numtracks; i++) {
		//printf("DEBUG: on track %d\n",i);
		t[i].t.sequence_number=i; //Default sequence number is just the chunk number in the file.  readtrackheader will update if necessary
		unsigned long offset=ftell(f)+t[i-1].t.bytes_remaining;
		//printf("DEBUG: old position:  %ld, bytes remaining: %ld, new position %ld\n",ftell(f),t[i-1].t.bytes_remaining,offset);
		f=fopen(filename,"rb");
		if(f==NULL) {
			return MIDI_ERR_REOPEN_FILE;
		}
		if(fseek(f,offset,SEEK_SET)) {
			return MIDI_ERR_SEEK_TRACK;
		}
		if((int errn=readtrackheader(header,f,&t[i].t))) {
			return errn;
		}
	}

	//Now we read in all the Meta events at time 0.
	for(int i=0; i<header->numtracks; i++) {

		while(munch_delta_t(&t[i].t) && t[i].t.delta_ticks==0) { //While there are more bytes (munch_delta_t(t) returns true) 
								     //and we are still at time 0
			uint8_t status=trackgetc(&t[i].t);//slurp up another byte
			if(status==0xFF) {
				if(int errn=handle_meta_event(header,&t[i].t)) {
					return errn;
				}
			} else {
				trackseek(&t[i].t,-1);//Go back a byte to undo the status byte we shouldn't have slurped up
				break;
			}
		}
		if(t[i].t.bytes_remaining==0) {
			t[i].t.last_timestamp=INT64_MAX;   //We give empty tracks the maximum timestamp, so that they will sort
							 //to the end of the array, where we can easily ignore them
							 //Note that we use INT64_MAX instead of UINT64_MAX, as to prevent 
							 //overflow when we are taking the difference
		}
		struct stat buf;
    		fstat(fileno(t[i].t.stream), &buf);
		printf("%ld bytes remaining %ld bytes remaining in file\n",t[i].t.bytes_remaining,buf.st_size-ftell(t[i].t.stream));
		if(ferror(t[i].t.stream))  {
			return MIDI_ERR_READING_EVENT;
		}
	}
	header->tracks=t;

	build_tree(header);
	cache_next_timestamp(header);
	return MIDI_ERR_NO_ERROR;
}


