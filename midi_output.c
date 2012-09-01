typedef struct {
	FILE* out;
	fpos_t length_offset; //This will keep track of the location in the file where we need to update the length after we have written the rest of the file
	uint64_t length; //this will keep track of the bytes written to the track.
	
	int64_t last_timestamp;
	float fractional_tick_error;
	float ticks_per_sample;

	LV2_URID midi_event_type;
} midi_output_file;


void write_BE_int(FILE* f,uint32_t i) {
	fputc((i&0xFF000000)>>24,f);
	fputc((i&0x00FF0000)>>16,f);
	fputc((i&0x0000FF00)>>8, f);
	fputc((i&0x000000FF)>>0, f);
}

void write_BE_short(FILE* f,uint16_t i) {
	fputc((i&0xFF00)>>8, f);
	fputc((i&0x00FF)>>0, f);
}

midi_error_code init_midi_output_file(midi_output_file* f,const char* filename, double sample_rate, LV2_URID midi_event_type) {
	f->out=fopen(filename,"wb");
	if(f->out==NULL) {
		return MIDI_ERR_OPEN_FILE;
	}
	f->last_timestamp=0;
	f->fractional_tick_error=0;

	f->midi_event_type=midi_event_type;

	int16_t division=0x7FFF; //The maximum division, for maximum accuracy ;)'
	int bps=2; //Default of 120 bpm
	f->ticks_per_timestamp=bps*division/sample_rate;
	
	//Write the MThd Header
	fputs("MThd",f->out);
	write_BE_int(f->out,6);
	write_BE_short(f->out,0);
	write_BE_short(f->out,1);
	write_BE_short(f->out,division);
	

	//Write the MTrk header
	fputs("MTrk",f->out);

	fgetpos(f->out,&f->length_offset);
	f->length=0;
	write_BE_int(f->out,0); //Write length

	if(ferror(out->out)) {
		return MIDI_ERR_WRITING_HEADER;
	} else {
		return MIDI_ERR_NO_ERROR;
	}
	//And we're off!
}

midi_error_code close_midi_output_file(midi_output_file* f) {
	if(f->out) {
		//Write the end of track event
		uint8_t EOT[]={0x00,0xFF,0x2F,0x00};  //A zero Delta-T, and then an end of track meta-event
		fwrite(EOT,1,sizeof(EOT),f->out);
		out->length+=sizeof(EOT); 
	
		//Update the lenght in the MTrk chunk to be accurate
		fsetpos(f->out, &f->length_offset);
		write_BE_int(f->out,f->length);
	
		if(ferror(f->out)) {
			return MIDI_ERR_FINALIZING;
		}
		//close the file
		if(fclose(f->out)) {
			return MIDI_ERR_CLOSE_FILE;
		} else {
			return MIDI_ERR_NO_ERROR;
		}
	} else {
		return MIDI_ERR_NO_ERROR;
	}
}

//from http://home.roadrunner.com/~jgglatt/tech/midifile.htm
void WriteVarLen(midi_output_file* out, uint32_t value)
{
   unsigned long buffer;
   buffer = value & 0x7F;

   while ( (value >>= 7) )
   {
     buffer <<= 8;
     buffer |= ((value & 0x7F) | 0x80);
   }

   while (TRUE)
   {
      fputc(buffer,out->out);
      out->length++;
      if (buffer & 0x80)
          buffer >>= 8;
      else
          break;
   }
}

midi_error_code write_MIDI_event(midi_output_file* out, LV2_Atom_Event* event) {
	if(event->body.type!=midi_event_type) {
		printf("Skipped writing non-midi event to MIDI file\n");
		return MIDI_ERR_NO_ERROR;
	}
	int64_t frames=event->frames;
	float ticks=(frames-out->last_timestamp)*out->ticks_per_sample+out->fractional_tick_error;

	float frac=modff(ticks+0.5,&ticks); //Rounds to nearest integer, and stores fractional error.
	frac-=0.5; 
	out->fractional_tick_error+=frac;
	out->last_timestamp;

	WriteVarLength(out,(uint32_t)ticks); //write timestamp
	fwrite(LV2_ATOM_BODY(event),1,event->body.size,out->out); //write event contents
	out->length+=event->body.size;
	if(ferror(out->out)) {
		return MIDI_ERR_WRITING_EVENT;
	} else {
		return MIDI_ERR_NO_ERROR;
	}
}

