#include <stdio.h>
#include <stdlib.h>
#include <sndfile.h>
#include <argtable2.h>
#include <lilv/lilv.h>
#include <string.h>
#include <math.h>
#include <regex.h>

//From lv2_simple_jack_host in slv2 (GPL code)
void list_plugins(const LilvPlugins* list)
{
	int j=1;
	LILV_FOREACH(plugins,i,list) {
		const LilvPlugin* p=lilv_plugins_get(list,i);
		printf("%d\t%s\n", j++, lilv_node_as_uri(lilv_plugin_get_uri(p)));

	}
}
const LilvPlugin* plugins_get_at(const LilvPlugins* plugins, unsigned int n) {
	unsigned int j=0;
	LILV_FOREACH(plugins,i,plugins) {
		if(j==n) {
			return lilv_plugins_get(plugins,i);
		}
		j++;
	}
	return NULL;
}

const LilvPlugin* getplugin(const char* name, const LilvPlugins* plugins, LilvWorld* lilvworld) {
	const char* end;
	long index=strtol(name,&end);
	if(index!=0 && *end=='\0') {
		return plugins_get_at(plugins,index-1);
	}
	
	const LilvPlugins* all_plugins=lilv_world_get_all_plugins(lilvworld);
	const LilvPlugin* plugin=NULL;
	unsigned int matches=0;
	LILV_FOREACH(plugins, i, all_plugins) {
		LilvPlugin* p = lilv_plugins_get(all_plugins,i);
		if(strstr(lilv_plugin_get_uri(p),name)) {
			printf("Note: Found matching plugin %s\n",lilv_plugin_get_uri(p));
			plugin=p;
			matches++;
		}
	}
	if(matches>1) {
		printf("Error: found multiple matching plugins\n");
		return null;
	} else {
		return plugin;
	}
}

unsigned int popcount(bool* connections, unsigned int numchannels) {
	unsigned int result=0;
	for(unsigned int i=0; i<numchannels; i++) {
		result+=connections[i];
	}
	return result;
}

void mix(float* buffer, sf_count_t framesread,unsigned int numchannels, unsigned int numplugins, unsigned int numin, bool connections[numplugins][numin][numchannels], unsigned int blocksize, float pluginbuffers[numplugins][numin][blocksize]) {
	for(unsigned int i=0; i<framesread; i++) {
		for(unsigned int plugnum=0; plugnum<numplugins; plugnum++) {
			for(unsigned int port=0; port<numin; port++) {
				unsigned int nummixed=0;
				pluginbuffers[plugnum][port][i]=0;
				for(unsigned int channel=0; channel<numchannels; channel++) {
					if(connections[plugnum][port][channel]) {
						nummixed++;
						pluginbuffers[plugnum][port][i]+=buffer[i*numchannels+channel];
					}
				}
				if(nummixed) {
					pluginbuffers[plugnum][port][i]/=nummixed;
				}
			}
		}
	}
}

void interleaveoutput(sf_count_t numread, unsigned int numplugins, unsigned int numout, unsigned int blocksize, float outputbuffers[numplugins][numout][blocksize], float sndfilebuffer[numout * blocksize]) {
	for(unsigned int plugin=0; plugin<numplugins; plugin++) {
		for(unsigned int port=0; port<numout; port++) {
			for(unsigned int i=0; i<numread; i++) {
				sndfilebuffer[plugin*numout*blocksize+i*numout+port]=outputbuffers[plugin][port][i];
			}
		}
	}
}

float getstartingvalue(float dflt,float min, float max) {
	if(!isnan(dflt)) {
		return dflt;
	} else {
		if(isnan(min)) {
			if(isnan(max)) {
				return 0;
			} else {
				return fmin(max,0);
			}
		} else {
			if(isnan(max)) {
				return fmax(min,0);
			} else {
				return (min+max)/2;
			}
		}
	}
}

inline char clipOutput(unsigned long size, float* buffer) {
	char clipped=0;
	for(unsigned int i=0; i<size; i++) {
		if(buffer[i]>1) {
			clipped=true;
			buffer[i]=1;
		}
	}
	for(unsigned int i=0; i<size; i++) {
		if(buffer[i]<-1) {
			clipped=true;
			buffer[i]=-1;
		}
	}
	return clipped;
}
void list_names(LilvWorld* lilvworld, const LilvPlugins* plugins, const char* plugin_name) {
	const LilvPlugin* plugin=getplugin(plugin_name,plugins,lilvworld);
	if(!plugin) {
		fprintf(stderr,"No such plugin %s\n",plugin_name);
		return;
	}
	LilvNode* input_class = lilv_new_uri(lilvworld, LILV_URI_INPUT_PORT);
	LilvNode* control_class = lilv_new_uri(lilvworld, LILV_URI_CONTROL_PORT);
	LilvNode* audio_class = lilv_new_uri(lilvworld, LILV_URI_AUDIO_PORT);
	uint32_t numports=lilv_plugin_get_num_ports(plugin);
	printf("==Audio Ports==\n");
	for(uint32_t port=0; port<numports; port++) {
		const LilvPort* p=lilv_plugin_get_port_by_index(plugin,port);
		if(lilv_port_is_a(plugin,p,input_class) && lilv_port_is_a(plugin,p,audio_class)) {
			printf("%s: %s\n",lilv_node_as_string(lilv_port_get_symbol(plugin,p)),lilv_node_as_string(lilv_port_get_name(plugin,p))); 			
		}
	}
	printf("==Control Ports==\n");
	for(uint32_t port=0; port<numports; port++) {
		const LilvPort* p=lilv_plugin_get_port_by_index(plugin,port);
		if(lilv_port_is_a(plugin,p,input_class) && lilv_port_is_a(plugin,p,control_class)) {
			printf("%s: %s\n",lilv_node_as_string(lilv_port_get_symbol(plugin,p)),lilv_node_as_string(lilv_port_get_name(plugin,p))); 			
		}
	}
	lilv_node_free(audio_class);
	lilv_node_free(input_class);
	lilv_node_free(control_class);
}

void sanitize_filename(const char* input char* output) { //List of special characters from wikipedia
	do {
		if(*input=='/' || 
		*input=='\' || 
		*input=='?' || 
		*input=='%' ||
		*input=='*' || 
		*input==':' || 
		*input=='|' || 
		*input=='"' || 
		*input=='<' || 
		*input=='>' || 
		*input=='.') {
			*output='_'; //Replace anything bad with an underscore! (tis true that there is a potential for collision!)
		} else {
			*output=*input;
		}
		output++;
		input++;
	} while(*input);
}

bool split_connection_arg(char** input, char** channel, int* pluginstance, char** port) {
	*channel=*input;
	*input=strchr(*input,':');
	if(!input) return true;	
	*input++='\0';
	char* instancenum=*input;
	*input=strchr(*input,'.');
	if(!input) return true;	
	if(input) {
		*input++='\0';
		*pluginstance=atoi(instancenum)-1;

	} else {
		*input=instancenum;
		*pluginstance=-1;
	}
	*port=*input;
	*input=strchr(*input,',');
	if(*input) {
		*input++='\0';
	}
	return true;
}

inline miditrack* resolve_midi_track(midifile* file, const char* identifier) {
	for(int i=0; i<file->numtracks; i++) {
		if(!strcmp(file->tracks[i].t.name,identifier)) {
			return &file->tracks[i].t;
		}
	}
	const char* final;
	while(isspace(*identifier)) identifier++;
	long index=strtol(identifier,&final);
	if(final==identifier) {
		return null;
	}
	for(int i=0; i<file->numtracks; i++) {
		if(file->tracks[i].t.sequence_number==index) {
			return &file->tracks[i].t;
		}
	}
	return null;
}

int my_asprintf(char** strp, const char* fmt, ...) {
    va_args args;
    va_start(args, fmt);
    size_t needed = vsnprintf(NULL, 0, fmt, args);
    va_end(args);
    char  *buffer = malloc(needed+1);
    va_start(args, fmt);
    needed = snprintf(buffer, needed, fmt,args);
    va_end(args);
    return needed;
}

/* X-Macro */
#define DEFINE_PROCESS {\
	float sndfilebuffer[numout * blocksize];\
	float buffer[numchannels * blocksize];\
	INITIALIZE_CLIPPED()\
	uint64_t absolute_frames=0;\
	sf_count_t numread;\
	while((numread = sf_readf_float(insndfile, buffer, blocksize)))	{\
		for(uint32_t inst=0; inst<numplugins; numplugins++) {\
			for(uint32_t port=0; port<nummidiin; port++) {\
				clear_event_buffer(&midi_input_buffers[inst][port],absolute_frames);\
			}\
		}\
		absolute_frames+=numread;\
		if(midi_error_code errn=transfer_events_before_timestamp(&midi_input_file,absolute_frames)) {\
			midi_print_error(errn);\
			goto cleanup_lv2;\
		}\
		for(uint32_t inst=0; inst<numplugins; numplugins++) {\
			for(uint32_t port=0; port<nummidiin; port++) {\
				lilv_instance_connect_port(instances[inst],midiininstance[port],midi_input_buffers[inst][port].buffer);\
			}\
		}\
		if(midi_error_code errn=transfer_events_before_timestamp(&midi_input_file,absolute_frames)) {\
			midi_print_error(errn);\
			goto cleanup_lv2;\
		}\
		mix(buffer,numread,numchannels,numplugins,numin,connections,blocksize,pluginbuffers);\
		for(unsigned int plugnum=0; plugnum<numplugins; plugnum++) {\
			lilv_instance_run(instances[plugnum],blocksize);\
		}\
		interleaveoutput(numread, numplugins, numout, blocksize, outputbuffers, sndfilebuffer);\
		CHECK_CLIPPED()\
		sf_writef_float(outsndfile, sndfilebuffer, numread);\
		for(uint32_t inst=0; inst<numplugins; numplugins++) {\
			for(uint32_t port=0; port<nummidiout; port++) {\
				LV2_Atom_Sequence* seq=MIDI_OUT_BUFFER(port);\
				LV2_ATOM_SEQUENCE_FOREACH(seq, ev) {\
					if(midi_error_code errn=write_MIDI_event(midi_output_files[inst][port],ev)) {\
						midi_print_error(errn);\
						goto cleanup_lv2;\
					}\
				}\
				seq->atom.type=chunk_uri;\
				seq->atom.size=buffersize-sizeof(LV2_Atom);\
			}\
		}\
	}\
}

#define INITIALIZE_CLIPPED()
#define CHECK_CLIPPED() 

#define PROCESS_NO_CHECK_CLIPPING() DEFINE_PROCESS()

#undef INITIALIZE_CLIPPED
#define INITIALIZE_CLIPPED() char clipped=0;

#undef CHECK_CLIPPED
#define CHECK_CLIPPED() if(!clipped && clipOutput(numread*numout,sndfilebuffer)) {\
			clipped=1;\
			printf("WARNING: Clipping output.  Try changing parameters of the plugin to lower the output volume, or if that's not possible, try lowering the volume of the input before processing.\n");\
		}

#define PROCESS_CHECK_CLIPPING() DEFINE_PROCESS()


int main(int argc, char** argv) {
	struct arg_lit *listopt=arg_lit1("l", "list","Lists all available LV2 plugins");
	struct arg_end *listend     = arg_end(20);
	void *listtable[] = {listopt,listend};
	if (arg_nullcheck(listtable) != 0) {
		printf("Error: insufficient memory\n");
		goto cleanup_listtable;
	}

	LilvWorld* lilvworld=lilv_world_new();
	if(lilvworld==NULL) {
		goto cleanup_listtable;
	}
	lilv_world_load_all(lilvworld);
	const LilvPlugins* plugins=lilv_world_get_all_plugins(lilvworld); 	

	if(!arg_parse(argc,argv,listtable)) {
		list_plugins(plugins);
		goto cleanup_lilvworld;
	}


	struct arg_lit *portnames = arg_lit1("n","nameports","List the names of the input ports of a given plugin");
	struct arg_str *pluginname = arg_str1(NULL,NULL,"plugin",NULL);
	struct arg_end *nameend=arg_end(20);
	void *listnamestable[]={portnames,pluginname,nameend};
	if (arg_nullcheck(listnamestable) != 0) {
		fprintf(stderr, "Error: insufficient memory\n");
		goto cleanup_listnamestable;
	}
	if(!arg_parse(argc,argv,listnamestable)) {
		list_names(lilvworld,plugins,pluginname->sval[0]);
		goto cleanup_listnamestable;
	}

	struct arg_rex *connectargs= arg_rexn("c","connect","(\\d+:(\\d+\\.)?\\w+,?)*","<int>:<audioport>",0,200,REG_EXTENDED,"Connect between audio file channels and plugin input channels.");
	struct arg_rex *midiconnectargs= arg_rexn(NULL,"mc,midi-connect","(\\d+:(\\d+\\.)?\\w+,?)*","<int>:<midiport>",0,200,REG_EXTENDED,"Connect between midi file tracks and plugin midi inputs.");

	struct arg_file* infile = arg_file1("i", NULL,"input", "Input sound file");
	struct arg_file* inmidifile = arg_file0(NULL, "mi,midi-input","input.midi", "Input MIDI file");
	struct arg_file* outfile = arg_file1("o", NULL,"output", "Output sound file");
	struct arg_file* outmidifile = arg_file0(NULL, "mo,midi-out-folder","output folder", "MIDI output folder");
	struct arg_rex* controls = arg_rexn("p", "parameters","(\\w+:\\w+,?)*","<controlport>:<float>",0,200,REG_EXTENDED, "Pass a value to a plugin control port.");
	pluginname = arg_str1(NULL,NULL,"plugin","The LV2 URI of the plugin");
	struct arg_int* blksize = arg_int0("b","blocksize","<int>","Chunk size in which the sound is processed.  This is frames, not samples.");
	struct arg_int* buffsize = arg_int0(NULL,"mb,midi-buffer-size","<int>","The size of the output buffer for midi events in bytes");
	struct arg_lit* mono = arg_lit0("m","mono","Mix all of the audio channels together before processing.");
	struct arg_lit* midimono = arg_lit0(NULL,"mm,midi-mono","Mix all of the MIDI tracks together before processing.");
	struct arg_lit* ignore_clipping = arg_lit0(NULL,"ignore-clipping", "Do not check for clipping.  This option is slightly faster");
	blksize->ival[0]=512;
	buffsize->ival[0]=1024;
	outmidifile->filename[0]="./";
	struct arg_end *endarg = arg_end(20);
	void *argtable[] = {infile, inmidifile, outfile, outmidifile, controls, connectargs, midiconnectargs, blksize,buffsize, mono,midimono ignore_clipping, pluginname, endarg};
	if (arg_nullcheck(argtable) != 0) {
		fprintf(stderr,"Error: insufficient memory\n");
		goto cleanup_argtable;
	}
	int nerrors = arg_parse(argc,argv,argtable);
	if(nerrors) {
		arg_print_errors(stderr,endarg,"lv2file");
		fprintf(stderr,"usage:\nlv2file\t");
		arg_print_syntaxv(stderr, listtable,"\n\t");
		arg_print_syntaxv(stderr, listnamestable,"\n\t");
		arg_print_syntaxv(stderr, argtable,"\n");
		arg_print_glossary_gnu(stderr, listtable);
		arg_print_glossary_gnu(stderr, listnamestable);
		arg_print_glossary_gnu(stderr, argtable);
		goto cleanup_argtable;
	}

	bool mixdown=mono->count;
	bool midi_mixdown=midimono->count;

	const LilvPlugin* plugin=getplugin(pluginname->sval[0],plugins,lilvworld);
	if(!plugin) {
		fprintf(stderr,"No such plugin %s\n", pluginname->sval[0]);
		goto cleanup_argtable;
	}
	SF_INFO formatinfo;
	formatinfo.format=0;
	SNDFILE* insndfile=sf_open(*(infile->filename), SFM_READ, &formatinfo);
	int sndfileerr=sf_error(insndfile) ;
	if(sndfileerr) {
		fprintf(stderr,"Error reading input file: %s\n",sf_error_number(sndfileerr));
		goto cleanup_sndfile;
	}

	unsigned int numchannels=formatinfo.channels;
	unsigned int blocksize=blksize->ival[0];
	unsigned int buffersize=lv2_atom_pad_size(buffsize->ival[0]);

	LilvNode* input_class = lilv_new_uri(lilvworld, LILV_URI_INPUT_PORT);
	LilvNode* output_class = lilv_new_uri(lilvworld, LILV_URI_OUTPUT_PORT);
	LilvNode* control_class = lilv_new_uri(lilvworld, LILV_URI_CONTROL_PORT);
	LilvNode* audio_class = lilv_new_uri(lilvworld, LILV_URI_AUDIO_PORT);
	LilvNode* atomPort_class = lilv_new_uri(lilvworld,  LV2_ATOM__AtomPort);
	LilvNode* midi_class = lilv_new_uri(lilvworld, LILV_URI_MIDI_EVENT);
	LilvNode* optional = lilv_new_uri(lilvworld, LILV_NS_LV2 "connectionOptional");

	{
		uint32_t numports=lilv_plugin_get_num_ports(plugin);
		unsigned int numout=0;
		uint32_t outindices[numports];
		unsigned int numin=0;
		uint32_t inindices[numports];
		unsigned int numcontrol=0;
		uint32_t controlindices[numports];
		unsigned int numcontrolout=0;
		uint32_t controloutindices[numports];
		unsigned int nummidiin=0;
		uint32_t midiinindices[numports];
		unsigned int nummidiout=0;
		uint32_t midioutindices[numports];

		bool portsproblem=false;
		for(uint32_t i=0; i<numports; i++) {
			const LilvPort* porti=lilv_plugin_get_port_by_index(plugin,i);	
			if(lilv_port_is_a(plugin,porti,audio_class)) {
				if(lilv_port_is_a(plugin,porti,input_class)) {
					inindices[numin++]=i;
				} else if(lilv_port_is_a(plugin,porti,output_class)) {
					outindices[numout++]=i;
				} else {
					fprintf(stderr, "Audio port not input or output\n");
					portsproblem=true;
				}
			} else if(lilv_port_is_a(plugin,porti,control_class)) {
				//We really only care about *input* control ports.
				if(lilv_port_is_a(plugin,porti,input_class)) {
					controlindices[numcontrol++]=i;
				} else if(lilv_port_is_a(plugin,porti,output_class)) {
					controloutindices[numcontrolout++]=i;
				} else {
					fprintf(stderr, "Control port not input or output\n");
					portsproblem=true;
				}
			} else if(lilv_port_is_a(plugin,porti, atomPort_class) && lilv_port_supports_event(plugin,porti, midi_class)) {
				if(lilv_port_is_a(plugin,porti,input_class)) {
					midiinindices[nummidiinl++]=i;
				} else if(lilv_port_is_a(plugin,porti,output_class)) {
					midioutindices[nummidiout++]=i;
				} else {
					fprintf(stderr, "Midi port not input or output\n");
					portsproblem=true;
				}
			}	if(!lilv_port_has_property(plugin,porti,optional)) {
				fprintf(stderr,"Error!  Unable to handle a required port \n");
				portsproblem=true;
			} 
		}

		lilv_node_free(input_class);
		lilv_node_free(output_class);
		lilv_node_free(control_class);
		lilv_node_free(audio_class);
		lilv_node_free(atomPort_class);
		lilv_node_free(midi_class);
		lilv_node_free(optional);
		if(portsproblem) {
			goto cleanup_sndfile;
		}
		if(mixdown && numin>1) {
			fprintf(stderr,"Cannot mix down audio to a single stream, as the plugin requires more than one audio input.");
			goto cleanup_sndfile;
		}
		if(midimixdown && nummidiin>1) {
			fprintf(stderr,"Cannot mix down MIDI to a single track, as the plugin requires more than one audio input.");
			goto cleanup_sndfile;
		}
		formatinfo.channels=numout;
		SNDFILE* outsndfile=sf_open(*(outfile->filename), SFM_WRITE, &formatinfo);

		sndfileerr=sf_error(outsndfile) ;
		if(sndfileerr) {
			fprintf(stderr,"Error opening output file: %s\n",sf_error_number(sndfileerr));
			goto cleanup_outfile;
		}
		
		if(!nummidiin && inmidifile->count) {
			fprintf(stderr,"Error, plugin does not take MIDI input\n");
			goto cleanup_outfile;
		}

		Symap* uri_mapper;
		LV2_URID_Map map_s;
		LV2_Feature map_feature;
		LV2_URID_Unmap unmap_s;
		LV2_Feature unmap_feature;
		{
			uri_mapper=symap_new();
			map_s.handle=uri_mapper;
			map_s.map=&symap_map;
			map_feature.URI=LV2_URID__map;
			map_feature.data=&map_s;
			unmap_s.handle=uri_mapper;
			unmap_s.unmap=&symap_map;
			unmap_feature.URI=LV2_URID__unmap;
			unmap_feature.data=&unmap_s;
			if(!uri_mapper) {
				fprintf(stderr,"Error initializing URI Mapper extension\n");
				goto cleanup_outfile;
			}
		}

		midifile midi_input_file;
		{
			if(outmidifile->count) {
				if(midi_error_code errn=open_midi_file(inmidifile->filename, &midi_input_file,formatinfo->samplerate))) {
					printf(stderr,"Error initializing MIDI input: ");					
					midi_print_error(errn);
					goto cleanup_midi_infile;
				}
			} else {
				midi_input_file->numtracks=0;
				midi_input_file->next_timestamp=UINT64_MAX;
		}


		{
			unsigned int numplugins=1; //First figure out the number of plugin instances to run.
			if(connectargs->count) {
				for(int i=0; i<connectargs->count; i++) {
					const char * connectionlist=connectargs->sval[i];
					while(*connectionlist) {
						char* nextcomma=strchr(connectionlist,',');
						char* nextcolon;
						if(nextcomma) {
							nextcolon=memchr(connectionlist,':',nextcomma-connectionlist);	
						} else {
							nextcolon=strchr(connectionlist,':');
						}
						if(!nextcolon) {
							fprintf(stderr, "Error parsing connection:  Expected colon between channel and port.\n");
							goto cleanup_midi_infile;
						}
						nextcolon++;
						int pluginstance=0;
						char* nextperiod=strchr(nextcolon,'.');
						if(nextperiod) {
							char tmpbuffer[nextperiod-nextcolon+1];
							memcpy(tmpbuffer,nextcolon,sizeof(char)*(nextperiod-nextcolon));
							tmpbuffer[nextperiod-nextcolon]=0;
							pluginstance=atoi(tmpbuffer)-1;
							if(pluginstance<0) {
								fprintf(stderr, "Invalid plugin instance specified");
								goto cleanup_midi_infile;
							}
						} else {
							nextperiod=nextcolon;
						}
						if(((unsigned)pluginstance)>=numplugins) {
							//Make sure we are instantiating enough instances of the plugin.
							numplugins=pluginstance+1;
						}
						if(nextcomma) {
							connectionlist=nextcomma+1;
						} else {
							break;
						}
					}
				}
			} else if(numin==1 && !mixdown) {
				numplugins=numchannels;
			}
			printf("Note: Running %i instances of the plugin.\n",numplugins);

			midi_output_file midi_output_files[numplugins][nummidiout];
			{
				memset(&midi_output_files,0,sizeof(midi_output_files));
					
				for(uint32_t port=0; port<nummidiout; port++) {
					//Do not need to free, kept internally.
					const char* symbol=lilv_node_as_string(lilv_port_get_symbol(plugin,lilv_plugin_get_port_by_index(plugin,inindices[port])));
					char portname[strlen(symbol)+1];
					sanitize_filename(symbol,portname);
					for(uint32_t inst=0;inst<numplugins; inst++) {
						char* fullname;
						my_asprintf(&fullname,"%s/%s_%u.midi",inmidifile->filename,portname,inst);
						midi_error_code errn=init_midi_output_file(&nummidiout[i],
									fullname, 
									formatinfo->samplerate,
									symap_map(uri_mapper,LV2_MIDI__MidiEvent));
						free(fullname);
						if(errn) {
							printf(stderr,"Error initializing MIDI output: ");					
							midi_print_error(errn);
							goto cleanup_midi_outfile;
						}
					}
				}
			}

			bool connections[numplugins][numin][numchannels];
			
			memset(connections,0,sizeof(connections));
			
			if(connectionargs->count) {
				for(int i=0; i<connectionargs->count; i++) {
				const char * connectionlist=connectionargs->sval[i];
					while(connectionlist) {
						char* channelstr;
						int pluginstance;
						char* portsymb;
						if(split_connection_arg(&connectionList, &channelstr, &pluginstance, &portsymb)) {
							fprintf(stderr,"Malformed -c (--connect) option\n");
							goto cleanup_midi_outfile;
						}
						int channel=atoi(channelstr);
						if(channel<0 || ((unsigned)channel)>=numchannels) {
							fprintf(stderr, "Input audio file does not have channel %u.  It has %u channels.\n",*pluginstance+1,numchannels);
							goto cleanup_midi_outfile;
						}
						for(uint32_t port=0; port<numin; port++) {
							//Do not need to free, kept internally.
							const char* symbol=lilv_node_as_string(lilv_port_get_symbol(plugin,lilv_plugin_get_port_by_index(plugin,inindices[port])));
							if(!strcmp(symbol,portsymb)) {
								foundmatch=true;
								if(pluginstance==-1) { //No instance specified, so we do all of them.
									for(pluginstance=0; pluginstance<numplugins; pluginstance++) {
										connections[pluginstance][port][channel]=true;
									}
								} else {
									connections[pluginstance][port][channel]=true;
								}
								break;
							}
						}
						if(!foundmatch) {
							fprintf(stderr, "Error: Port with symbol %s does not exist.\n",nextcolon);
							goto cleanup_midi_outfile;
						}
					}
				}
				printf("Note: Only making user specified audio connections.\n");
			} else {
				if(numin==numchannels) {
					printf("Note: Mapping audio channels to plugin ports based on ordering\n");
					for(unsigned int i=0; i<numin; i++) {
						connections[0][i][i]=true;
					}
				} else if(numin==1) {
					if(mixdown) {
						printf("Note: Down mixing all channels to a single plugin input\n");
						for(unsigned int i=0; i<numin; i++) {
							connections[0][0][i]=true;
						}
					} else {
						printf("Note: Running an instance of the plugin per channel\n");
						for(unsigned int i=0; i<numchannels; i++) {
							connections[i][0][i]=true;
						}
					} 
				}else if(numchannels>numin) {
					printf("Note: Extra channels ignored when mapping audio channels to plugin ports\n");
					for(unsigned int i=0; i<numin; i++) {
						connections[0][i][i]=true;
					}
				} else {
					fprintf(stderr,"Error: Not enough input audio channels to connect all of the plugin's ports.  Please manually specify connections\n");
					goto cleanup_midi_outfile;
				}
			}

			event_buffer midi_input_buffers[numplugins][nummidiin];
			{
				for(int inst=0; inst<numplugins; inst++) {
					for(int i=0; i<num_midi_in; i++) {
						init_event_buffer(&midi_input_buffers[inst][i], uri_mapper,buffersize);
					}
				}
			}


			if(midiconnectionargs->count) {
				for(int i=0; i<midiconnectionargs->count; i++) {
				const char * connectionlist=midiconnectionargs->sval[i];
					while(connectionlist) {
						char* track;
						int pluginstance;
						char* portsymb;
						if(split_connection_arg(&connectionList, &track, &pluginstance, &portsymb)) {
							fprintf(stderr,"Malformed -mc (--midi-connect) option\n");
							goto cleanup_midi_outfile;
						}
						miditrack* intrack=resolve_midi_track(&midi_input_file,track);
						if(!intrack) {
							fprintf(stderr,"Unable to find specified midi track: %s\n",track);
							goto cleanup_midi_outfile;
						}
						for(uint32_t port=0; port<nummidiin; port++) {
							//Do not need to free, kept internally.
							const char* symbol=lilv_node_as_string(lilv_port_get_symbol(plugin,lilv_plugin_get_port_by_index(plugin,midiinindices[port])));
							if(!strcmp(symbol,portsymb)) {
								foundmatch=true;
								if(pluginstance==-1) { //No instance specified, so we do all of them.
									for(pluginstance=0; pluginstance<numplugins; pluginstance++) {
										miditrack_connections_add_connection(&intrack->connections, midi_input_buffers[pluginstance][port]);
									}
								} else {
									miditrack_connections_add_connection(&intrack->connections, midi_input_buffers[pluginstance][port]);
								}
								break;
							}
						}
						if(!foundmatch) {
							fprintf(stderr, "Error: Port with symbol %s does not exist.\n",nextcolon);
							goto cleanup_midi_outfile;
						}
					}
				}
				printf("Note: Only making user specified midi connections.\n");
			} else {
				if (nummidiin==midi_input_file.numtracks) {
					printf("Note: Connecting MIDI tracks based on ordering\n");
					for(int track=0; track<midi_input_file.numtracks; track++) {
						miditrack* t=&midi_input_file.tracks[track];
						for(int inst=0; inst<numplugins; inst++) {
							miditrack_connections_add_connection(&t->connections, midi_input_buffers[inst][midiinindicess[t->sequence_number]]);
						}
					}
				}  else if(nummidiin==1) {
					if(midimixdown) {
						printf("Note: Mixing all MIDI tracks together for input\n");
						for(int inst=0; inst<numplugins; inst++) {
							for(int track=0; track<midi_input_file.numtracks; track++) {
								miditrack_connections_add_connection(&midi_input_file.tracks[track]->connections, midi_input_buffers[inst][midiinindicess[0]]);
							}
						}
					} else if(numplugins=midi_input_file.numtracks) {
						printf("Note: Connecting each MIDI track to a different plugin instance\n");
						for(int track=0; track<midi_input_file.numtracks; track++) {
							miditrack* t=&midi_input_file.tracks[track];
							miditrack_connections_add_connection(&t->connections, midi_input_buffers[track][midiinindicess[0]]);
						}
					} else {
						fprintf(stderr,"Error: There are multiple MIDI tracks in the input file specified, but the plugin takes only one MIDI input.  Please manually specify your connections using --midi-connect or --midi-mono.\n");
						goto cleanup_midi_outfile;
					}
				} else if(nummidiin<midi_input_file.numtracks) {
					printf("Note: Extra MIDI tracks ignored when mapping tracks to plugin ports\n");
					for(int track=0; track<midi_input_file.numtracks; track++) {
						miditrack* t=&midi_input_file.tracks[track];
						if(t->sequence_number<nummidiin) {			
							for(int inst=0; inst<numplugins; inst++) {
								miditrack_connections_add_connection(&t->connections, midi_input_buffers[inst][midiinindicess[t->sequence_number]]);
							}
						}
					}
				} else {
					fprintf(stderr,"Error: Not enough MIDI tracks to connect all of the plugin's ports.  Please manually specify connections\n");
					goto cleanup_midi_outfile;
				}
			}

			
			LilvInstance* instances[numplugins];
			LV2_Feature* features[]={map_feature,unmap_feature,NULL};
			for(unsigned int i=0; i<numplugins; i++) {
				instances[i]=lilv_plugin_instantiate (plugin, formatinfo.samplerate, &features);
				lilv_instance_activate(instances[i]); 
			}
			{
				float pluginbuffers[numplugins][numin][blocksize];
				memset(pluginbuffers,0,sizeof(pluginbuffers));
				float outputbuffers[numplugins][numout][blocksize];
				memset(outputbuffers,0,sizeof(outputbuffers));

				float controlports[numcontrol];
				memset(controlports,0,sizeof(controlports));
				float controloutports[numcontrolout];

				float minvalues[numports];
				float maxvalues[numports];
				float defaultvalues[numports];

				lilv_plugin_get_port_ranges_float(plugin,minvalues,maxvalues,defaultvalues);
				for(unsigned int port=0; port<numcontrol; port++) {
					unsigned int portindex=controlindices[port];
					controlports[port]=getstartingvalue(defaultvalues[portindex],minvalues[portindex],maxvalues[portindex]);
				}
				if(controls->count) {
					for(int i=0; i<controls->count; i++) {
						const char * parameters=controls->sval[i];
						while(*parameters) {
							char* nextcomma=strchr(parameters,',');
							if(nextcomma) {
								*nextcomma=0;
							}
							char* nextcolon=strchr(parameters,':');
							if(nextcolon) {
								*nextcolon=0;
							} else {
								fprintf(stderr, "Error parsing parameters:  Expected colon between port and value.\n");
								goto cleanup_midi_outfile;
							}
							nextcolon++;
							float value=strtof(nextcolon,NULL);
							bool foundmatch=false;
							for(uint32_t port=0; port<numcontrol; port++) {
								//Do not need to free, kept internally.
								const char* symbol=lilv_node_as_string(lilv_port_get_symbol(plugin,lilv_plugin_get_port_by_index(plugin,controlindices[port])));
								if(!strcmp(symbol,parameters)) {
									controlports[port]=value;
									foundmatch=true;
									break;
								}
							}
							if(!foundmatch) {
								fprintf(stderr, "Error: Port with symbol %s does not exist.\n",parameters);
								goto cleanup_midi_outfile;
							}
							if(nextcomma) {
								parameters=nextcomma+1;
							} else {
								break;
							}
						}
					}
				}
				uint8_t midioutbuffers[numplugins][nummidiout][buffersize];
#define MIDI_OUT_BUFFER(i,p) ((LV2_Atom_Sequence*)midioutbuffers[i][p])
				
				uint32_t chunk_uri=symap_map(uri_mapper,LV2_ATOM__Chunk);
				for(int i=0; i<numplugins; i++) {
					for(int port=0; port<nummidiout; port++) {
						LV2_Atom_Sequence* seq=MIDI_OUT_BUFFER(i,port);
						seq->atom.type=chunk_uri;
						seq->atom.size=buffersize-sizeof(LV2_Atom);
					}
				}
				for(unsigned int i=0; i<numplugins; i++) {
					for(unsigned int port=0; port<numin; port++) {
						lilv_instance_connect_port(instances[i],inindices[port],pluginbuffers[i][port]);
					}
					for(unsigned int port=0; port<numout; port++) {
						lilv_instance_connect_port(instances[i],outindices[port],outputbuffers[i][port]);
					}
					for(unsigned int port=0; port<numcontrol; port++) {
						lilv_instance_connect_port(instances[i],controlindices[port],&controlports[port]);
					}
					for(unsigned int port=0; port<numcontrolout; port++) {
						lilv_instance_connect_port(instances[i],controloutindices[port],&controloutports[port]);
					}
					for(unsigned int port=0; port<nummidiout; port++) {
						lilv_instance_connect_port(instances[i],midioutindices[i][port],MIDI_OUT_BUFFER(i,port));
					}
				}
				if(ignore_clipping->count) {
					PROCESS_NO_CHECK_CLIPPING()
				} else {
					PROCESS_CHECK_CLIPPING()
				}
			}

			cleanup_lv2:
				for(unsigned int i=0; i<numplugins; i++) {
					lilv_instance_deactivate(instances[i]);
					lilv_instance_free(instances[i]);
				}
			cleanup_midi_outfile:
				for(int inst=0; inst<numplugins; inst++) {
					for(int i=0; i<nummidiout; i++) {
						close_midi_output_file(&midi_output_files[inst][i]);
					}
				}
		}
		cleanup_midi_infile:
			close_midi_input_file(&midi_input_file)
		cleanup_symap:
			symap_free(uri_mapper);
		cleanup_outfile:
			if(sf_close  (outsndfile)) {
				fprintf(stderr,"Error closing input file!\n");
			}
	}

	cleanup_sndfile:
		if(sf_close(insndfile)) {
			fprintf(stderr,"Error closing input file!\n");
		}

	cleanup_argtable:
		arg_freetable(argtable,sizeof(argtable)/sizeof(argtable[0]));
	cleanup_listnamestable:
		arg_freetable(listnamestable,sizeof(listnamestable)/sizeof(listnamestable[0]));
	cleanup_lilvworld:
		lilv_world_free(lilvworld);  
	cleanup_listtable:
		arg_freetable(listtable,sizeof(listtable)/sizeof(listtable[0]));

}
