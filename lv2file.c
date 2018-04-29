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
	int index=atoi(name);
	if(index!=0) {
		return plugins_get_at(plugins,index-1);
	} else {
		LilvNode* plugin_uri = lilv_new_uri(lilvworld, name);
		if(!plugin_uri) {
			return NULL;
		}
		const LilvPlugin* plugin = lilv_plugins_get_by_uri(plugins, plugin_uri);
		lilv_node_free(plugin_uri);
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

/* X-Macro */
#define DEFINE_PROCESS (unsigned int blocksize, unsigned int numchannels, unsigned int numin, unsigned int numout, unsigned int numplugins, bool connections[numplugins][numin][numchannels], float pluginbuffers[numplugins][numin][blocksize], float outputbuffers[numplugins][numout][blocksize],LilvInstance* instances[numplugins],SNDFILE* insndfile, SNDFILE* outsndfile){\
	float sndfilebuffer[numout * blocksize];\
	float buffer[numchannels * blocksize];\
	INITIALIZE_CLIPPED()\
	sf_count_t numread;\
	while((numread = sf_readf_float(insndfile, buffer, blocksize)))	{\
		mix(buffer,numread,numchannels,numplugins,numin,connections,blocksize,pluginbuffers);\
		for(unsigned int plugnum=0; plugnum<numplugins; plugnum++) {\
			lilv_instance_run(instances[plugnum],blocksize);\
		}\
		interleaveoutput(numread, numplugins, numout, blocksize, outputbuffers, sndfilebuffer);\
		CHECK_CLIPPED()\
		sf_writef_float(outsndfile, sndfilebuffer, numread);\
	}\
}

#define INITIALIZE_CLIPPED()
#define CHECK_CLIPPED()

void process_no_check_clipping DEFINE_PROCESS

#undef INITIALIZE_CLIPPED
#define INITIALIZE_CLIPPED() char clipped=0;

#undef CHECK_CLIPPED
#define CHECK_CLIPPED() if(!clipped && clipOutput(numread*numout,sndfilebuffer)) {\
			clipped=1;\
			printf("WARNING: Clipping output.  Try changing parameters of the plugin to lower the output volume, or if that's not possible, try lowering the volume of the input before processing.\n");\
		}

void process_check_clipping DEFINE_PROCESS

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

	struct arg_file* infile = arg_file1("i", NULL,"input", "Input sound file");
	struct arg_file* outfile = arg_file1("o", NULL,"output", "Output sound file");
	struct arg_rex* controls = arg_rexn("p", "parameters","(\\w+:\\w+,?)*","<controlport>:<float>",0,200,REG_EXTENDED, "Pass a value to a plugin control port.");
	pluginname = arg_str1(NULL,NULL,"plugin","The LV2 URI of the plugin");
	struct arg_int* blksize = arg_int0("b","blocksize","<int>","Chunk size in which the sound is processed.  This is frames, not samples.");
	struct arg_lit* mono = arg_lit0("m","mono","Mix all of the channels together before processing.");
	struct arg_lit* ignore_clipping = arg_lit0(NULL,"ignore-clipping", "Do not check for clipping.  This option is slightly faster");
	blksize->ival[0]=512;
	struct arg_end *endarg = arg_end(20);
	void *argtable[] = {infile, outfile,controls,connectargs,blksize,mono,ignore_clipping,pluginname, endarg};
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

	LilvNode* input_class = lilv_new_uri(lilvworld, LILV_URI_INPUT_PORT);
	LilvNode* output_class = lilv_new_uri(lilvworld, LILV_URI_OUTPUT_PORT);
	LilvNode* control_class = lilv_new_uri(lilvworld, LILV_URI_CONTROL_PORT);
	LilvNode* audio_class = lilv_new_uri(lilvworld, LILV_URI_AUDIO_PORT);
	LilvNode* event_class = lilv_new_uri(lilvworld, LILV_URI_EVENT_PORT);
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
			} else if(!lilv_port_has_property(plugin,porti,optional)) {
				fprintf(stderr,"Error!  Unable to handle a required port \n");
				portsproblem=true;
			}
		}

		lilv_node_free(input_class);
		lilv_node_free(output_class);
		lilv_node_free(control_class);
		lilv_node_free(audio_class);
		lilv_node_free(event_class);
		lilv_node_free(midi_class);
		lilv_node_free(optional);
		if(portsproblem) {
			goto cleanup_sndfile;
		}
		formatinfo.channels=numout;
		SNDFILE* outsndfile=sf_open(*(outfile->filename), SFM_WRITE, &formatinfo);

		sndfileerr=sf_error(outsndfile) ;
		if(sndfileerr) {
			fprintf(stderr,"Error reading output file: %s\n",sf_error_number(sndfileerr));
			goto cleanup_outfile;
		}

		{
			unsigned int numplugins=1;
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
							goto cleanup_outfile;
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
								goto cleanup_outfile;
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
			bool connections[numplugins][numin][numchannels];
			memset(connections,0,sizeof(connections));

			LilvInstance* instances[numplugins];
			if(connectargs->count) {
				for(int i=0; i<connectargs->count; i++) {
					const char * connectionlist=connectargs->sval[i];
					while(*connectionlist) {
						int channel=atoi(connectionlist)-1;
						if(channel<0|| ((unsigned)channel)>=numchannels) {
							fprintf(stderr, "Input sound file does not have channel %u.  It has %u channels.\n",channel+1,numchannels);
							goto cleanup_outfile;
						}

						char* nextcomma=strchr(connectionlist,',');
						if(nextcomma) {
							*nextcomma=0;
						}
						char* nextcolon=strchr(connectionlist,':');
						//Will not be nill otherwise it would have been caught when counting plugin instances
						*nextcolon=0;
						nextcolon++;
						unsigned int pluginstance=0;
						char* nextperiod=strchr(nextcolon,'.');
						if(nextperiod) {
							*nextperiod=0;
							pluginstance=atoi(nextcolon+1)-1;
						} else {
							nextperiod=nextcolon;
						}
						bool foundmatch=false;
						for(uint32_t port=0; port<numin; port++) {
							//Do not need to free, kept internally.
							const char* symbol=lilv_node_as_string(lilv_port_get_symbol(plugin,lilv_plugin_get_port_by_index(plugin,inindices[port])));
							if(!strcmp(symbol,nextperiod)) {
								connections[pluginstance][port][channel]=true;
								foundmatch=true;
								break;
							}
						}
						if(!foundmatch) {
							fprintf(stderr, "Port with symbol %s does not exist.\n",nextcolon);
						}
						if(nextcomma) {
							connectionlist=nextcomma+1;
						} else {
							break;
						}
					}
				}
				printf("Note: Only making user specified connections.\n");
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
					printf("Note: Extra channels ignored when mapping channels to plugin ports\n");
					for(unsigned int i=0; i<numin; i++) {
						connections[0][i][i]=true;
					}
				} else {
					fprintf(stderr,"Error: Not enough input channels to connect all of the plugin's ports.  Please manually specify connections\n");
					goto cleanup_outfile;
				}
			}
			for(unsigned int i=0; i<numplugins; i++) {
				instances[i]=lilv_plugin_instantiate (plugin, formatinfo.samplerate , NULL);
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
								goto cleanup_outfile;
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
								fprintf(stderr, "WARNING: Port with symbol %s does not exist.\n",parameters);
							}
							if(nextcomma) {
								parameters=nextcomma+1;
							} else {
								break;
							}
						}
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
				}
				if(ignore_clipping->count) {
					process_no_check_clipping(blocksize, numchannels, numin, numout, numplugins, connections, pluginbuffers, outputbuffers, instances, insndfile, outsndfile);
				} else {
					process_check_clipping(blocksize, numchannels, numin, numout, numplugins, connections, pluginbuffers, outputbuffers, instances, insndfile, outsndfile);
				}
			}

			//cleanup_lv2:
				for(unsigned int i=0; i<numplugins; i++) {
					lilv_instance_deactivate(instances[i]);
					lilv_instance_free(instances[i]);
				}
		}
		cleanup_outfile:
			if(sf_close  (outsndfile)) {
				fprintf(stderr,"Error closing input file!\n");
			}
	}

	cleanup_sndfile:
		if(sf_close  (insndfile)) {
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
