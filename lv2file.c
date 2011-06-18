#include <stdio.h>
#include <stdlib.h>
#include <sndfile.h>
#include <argtable2.h>
#include <slv2/slv2.h>
#include <string.h>
#include <math.h>
#include <regex.h>

//From lv2_simple_jack_host in slv2 (GPL code)
void list_plugins(SLV2Plugins list)
{
	for (unsigned i=0; i < slv2_plugins_size(list); ++i) {
		SLV2Plugin p = slv2_plugins_get_at(list, i);
		printf("%d\t%s\n", i+1, slv2_value_as_uri(slv2_plugin_get_uri(p)));
	}
}

SLV2Plugin getplugin(const char* name, SLV2Plugins plugins, SLV2World slv2world) {
	int index=atoi(name);
	if(index!=0) {
		return slv2_plugins_get_at(plugins,index-1);
	} else {
		SLV2Value plugin_uri = slv2_value_new_uri(slv2world, name);
		SLV2Plugin plugin = slv2_plugins_get_by_uri(plugins, plugin_uri);
		slv2_value_free(plugin_uri);
		return plugin;
	}
}

unsigned int popcount(bool* connections, unsigned int numchannels) {
	unsigned int result=0;
	for(int i=0; i<numchannels; i++) {
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
	for(int plugin=0; plugin<numplugins; plugin++) {
		for(int port=0; port<numout; port++) {
			for(int i=0; i<numread; i++) {
				sndfilebuffer[plugin*numout*blocksize+i*numout+port]=outputbuffers[plugin][port][i];
			}
		}
	}
}

void connectpluginbuffers(float* pluginbuffers, SLV2Instance instance) {
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


int main(int argc, char** argv) {
	struct arg_lit *listopt=arg_lit1("l", "list","Lists all available LV2 plugins");
	struct arg_end *listend     = arg_end(20);
	void *listtable[] = {listopt,listend};
	if (arg_nullcheck(listtable) != 0) {
		printf("Error: insufficient memory\n");
		goto cleanup_listtable;
	}

	SLV2World slv2world=slv2_world_new();
	if(slv2world==NULL) {
		goto cleanup_listtable;
	}
	slv2_world_load_all(slv2world);
	SLV2Plugins plugins=slv2_world_get_all_plugins(slv2world); 	

	if(!arg_parse(argc,argv,listtable)) {
		list_plugins(plugins);
		goto cleanup_slv2world;
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
		SLV2Plugin plugin=getplugin(pluginname->sval[0],plugins,slv2world);
		slv2_plugins_free(slv2world,plugins);
		if(!plugin) {
			fprintf(stderr,"No such plugin %s\n",pluginname->sval[0]);
			goto cleanup_listnamestable;
		}
		SLV2Value input_class = slv2_value_new_uri(slv2world, SLV2_PORT_CLASS_INPUT);
		SLV2Value control_class = slv2_value_new_uri(slv2world, SLV2_PORT_CLASS_CONTROL);
		SLV2Value audio_class = slv2_value_new_uri(slv2world, SLV2_PORT_CLASS_AUDIO);
		uint32_t numports=slv2_plugin_get_num_ports(plugin);
		printf("==Audio Ports==\n");
		for(uint32_t port=0; port<numports; port++) {
			SLV2Port p=slv2_plugin_get_port_by_index(plugin,port);
			if(slv2_port_is_a(plugin,p,input_class) && slv2_port_is_a(plugin,p,audio_class)) {
				printf("%s: %s\n",slv2_value_as_string(slv2_port_get_symbol(plugin,p)),slv2_value_as_string(slv2_port_get_name(plugin,p))); 			
			}
		}
		printf("==Control Ports==\n");
		for(uint32_t port=0; port<numports; port++) {
			SLV2Port p=slv2_plugin_get_port_by_index(plugin,port);
			if(slv2_port_is_a(plugin,p,input_class) && slv2_port_is_a(plugin,p,control_class)) {
				printf("%s: %s\n",slv2_value_as_string(slv2_port_get_symbol(plugin,p)),slv2_value_as_string(slv2_port_get_name(plugin,p))); 			
			}
		}
		slv2_value_free(audio_class);
		slv2_value_free(input_class);
		slv2_value_free(control_class);
		goto cleanup_listnamestable;
	}

	struct arg_rex *connectargs= arg_rexn("c","connect","(\\d+:(\\d+\\.)?\\w+,?)*","<int>:<audioport>",0,200,REG_EXTENDED,"Connect between audio file channels and plugin input channels.");

	struct arg_file *infile = arg_file1("i", NULL,"input", "Input sound file");
	struct arg_file *outfile = arg_file1("o", NULL,"output", "Output sound file");
	struct arg_rex *controls = arg_rexn("p", "parameters","(\\w+:\\w+,?)*","<controlport>:<float>",0,200,REG_EXTENDED, "Pass a value to a plugin control port.");
	pluginname = arg_str1(NULL,NULL,"plugin","The LV2 URI of the plugin");
	struct arg_int *blksize = arg_int0("b","blocksize","<int>","Chunk size in which the sound is processed.  This is frames, not samples.");
	struct arg_lit *mono = arg_lit0("m","mono","Mix all of the channels together before processing.");
	blksize->ival[0]=512;
	struct arg_end *endarg = arg_end(20);
	void *argtable[] = {infile, outfile,controls,connectargs,blksize,mono,pluginname, endarg};
	if (arg_nullcheck(argtable) != 0) {
		fprintf(stderr,"Error: insufficient memory\n");
		goto cleanup_argtable;
	}
	int nerrors = arg_parse(argc,argv,argtable);
	if(nerrors) {
		fprintf(stderr,"lv2file\t");
		arg_print_syntaxv(stderr, listtable,"\n\t");
		arg_print_syntaxv(stderr, listnamestable,"\n\t");
		arg_print_syntaxv(stderr, argtable,"\n");
		arg_print_glossary_gnu(stderr, listtable);
		arg_print_glossary_gnu(stderr, listnamestable);
		arg_print_glossary_gnu(stderr, argtable);
		goto cleanup_argtable;
	}

	bool mixdown=mono->count;

	SLV2Plugin plugin=getplugin(pluginname->sval[0],plugins,slv2world);
	slv2_plugins_free(slv2world,plugins);
	if(!plugin) {
		fprintf(stderr,"No such plugin %s\n",pluginname->sval[0]);
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

	SLV2Value input_class = slv2_value_new_uri(slv2world, SLV2_PORT_CLASS_INPUT);
	SLV2Value output_class = slv2_value_new_uri(slv2world, SLV2_PORT_CLASS_OUTPUT);
	SLV2Value control_class = slv2_value_new_uri(slv2world, SLV2_PORT_CLASS_CONTROL);
	SLV2Value audio_class = slv2_value_new_uri(slv2world, SLV2_PORT_CLASS_AUDIO);
	SLV2Value event_class = slv2_value_new_uri(slv2world, SLV2_PORT_CLASS_EVENT);
	SLV2Value midi_class = slv2_value_new_uri(slv2world, SLV2_EVENT_CLASS_MIDI);
	SLV2Value optional = slv2_value_new_uri(slv2world, SLV2_NAMESPACE_LV2 "connectionOptional");

	{
		uint32_t numports=slv2_plugin_get_num_ports(plugin);
		int numout=0;
		uint32_t outindices[numports];
		int numin=0;
		uint32_t inindices[numports];
		int numcontrol=0;
		uint32_t controlindices[numports];
		int numcontrolout=0;
		uint32_t controloutindices[numports];

		bool portsproblem=false;
		for(uint32_t i=0; i<numports; i++) {
			SLV2Port porti=slv2_plugin_get_port_by_index(plugin,i);	
			if(slv2_port_is_a(plugin,porti,audio_class)) {
				if(slv2_port_is_a(plugin,porti,input_class)) {
					inindices[numin++]=i;
				} else if(slv2_port_is_a(plugin,porti,output_class)) {
					outindices[numout++]=i;
				} else {
					fprintf(stderr, "Audio port not input or output\n");
					portsproblem=true;
				}
			} else if(slv2_port_is_a(plugin,porti,control_class)) {
				//We really only care about *input* control ports.
				if(slv2_port_is_a(plugin,porti,input_class)) {
					controlindices[numcontrol++]=i;
				} else if(slv2_port_is_a(plugin,porti,output_class)) {
					controloutindices[numcontrolout++]=i;
				} else {
					fprintf(stderr, "Control port not input or output\n");
					portsproblem=true;
				}
			} else if(!slv2_port_has_property(plugin,porti,optional)) {
				fprintf(stderr,"Error!  Unable to handle a required port \n");
				portsproblem=true;
			} 
		}

		slv2_value_free(input_class);
		slv2_value_free(output_class);
		slv2_value_free(control_class);
		slv2_value_free(audio_class);
		slv2_value_free(event_class);
		slv2_value_free(midi_class);
		slv2_value_free(optional);
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
						unsigned int pluginstance=0;
						char* nextperiod=strchr(nextcolon,'.');
						if(nextperiod) {
							char tmpbuffer[nextperiod-nextcolon+1];
							memcpy(tmpbuffer,nextcolon,sizeof(char)*(nextperiod-nextcolon));
							tmpbuffer[nextperiod-nextcolon]=0;
							pluginstance=atoi(tmpbuffer);
							if(pluginstance<0) {
								fprintf(stderr, "Invalid plugin instance specified");
								goto cleanup_outfile;
							}
						} else {
							nextperiod=nextcolon;
						}
						if(pluginstance>=numplugins) {
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

			SLV2Instance instances[numplugins];
			if(connectargs->count) {
				for(int i=0; i<connectargs->count; i++) {
					const char * connectionlist=connectargs->sval[i];
					while(*connectionlist) {
						unsigned int channel=atoi(connectionlist)-1;
						if(channel>=numchannels || channel<0) {
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
							const char* symbol=slv2_value_as_string(slv2_port_get_symbol(plugin,slv2_plugin_get_port_by_index(plugin,inindices[port])));
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
					for(int i=0; i<numin; i++) {
						connections[0][i][i]=true;
					}
				} else if(numin==1) {
					if(mixdown) {
						printf("Note: Down mixing all channels to a single plugin input\n");
						for(int i=0; i<numin; i++) {
							connections[0][0][i]=true;
						}
					} else {
						printf("Note: Running an instance of the plugin per channel\n");
						for(int i=0; i<numchannels; i++) {
							connections[i][0][i]=true;
						}
					} 
				}else if(numchannels>numin) {
					printf("Note: Extra channels ignored when mapping channels to plugin ports\n");
					for(int i=0; i<numin; i++) {
						connections[0][i][i]=true;
					}
				} else {
					fprintf(stderr,"Error: Not enough input channels to connect all of the plugin's ports.  Please manually specify connections\n");
					goto cleanup_outfile;
				}
			}
			for(int i=0; i<numplugins; i++) {
				instances[i]=slv2_plugin_instantiate (plugin, formatinfo.samplerate , NULL);
				slv2_instance_activate(instances[i]); 
			}
			{
				float buffer[numchannels * blocksize];
				float pluginbuffers[numplugins][numin][blocksize];
				memset(pluginbuffers,0,sizeof(pluginbuffers));
				float outputbuffers[numplugins][numout][blocksize];
				memset(outputbuffers,0,sizeof(outputbuffers));

				float sndfilebuffer[numout * blocksize];
				float controlports[numcontrol];
				memset(controlports,0,sizeof(controlports));
				float controloutports[numcontrolout];

				float minvalues[numports];
				float maxvalues[numports];
				float defaultvalues[numports];

				slv2_plugin_get_port_ranges_float(plugin,minvalues,maxvalues,defaultvalues);
				for(int port=0; port<numcontrol; port++) {
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
								const char* symbol=slv2_value_as_string(slv2_port_get_symbol(plugin,slv2_plugin_get_port_by_index(plugin,controlindices[port])));
								if(!strcmp(symbol,parameters)) {
									controlports[port]=value;
									foundmatch=true;
									break;
								}
							}
							if(!foundmatch) {
								fprintf(stderr, "Port with symbol %s does not exist.\n",nextcolon+1);
							}
							if(nextcomma) {
								parameters=nextcomma+1;
							} else {
								break;
							}
						}
					}
				}

				for(int i=0; i<numplugins; i++) {
					for(int port=0; port<numin; port++) {
						slv2_instance_connect_port(instances[i],inindices[port],pluginbuffers[i][port]);
					}
					for(int port=0; port<numout; port++) {
						slv2_instance_connect_port(instances[i],outindices[port],outputbuffers[i][port]);
					}
					for(int port=0; port<numcontrol; port++) {
						slv2_instance_connect_port(instances[i],controlindices[port],&controlports[port]);
					}
					for(int port=0; port<numcontrolout; port++) {
						slv2_instance_connect_port(instances[i],controloutindices[port],&controloutports[port]);
					}
				}
				sf_count_t numread;
				while((numread = sf_readf_float(insndfile, buffer, blocksize)))	{
					mix(buffer,numread,numchannels,numplugins,numin,connections,blocksize,pluginbuffers);
					for(int plugnum=0; plugnum<numplugins; plugnum++) {
						slv2_instance_run(instances[plugnum],blocksize);
					}
					interleaveoutput(numread, numplugins, numout, blocksize, outputbuffers, sndfilebuffer);
					sf_writef_float(outsndfile, sndfilebuffer, numread);
				}
			}

			cleanup_lv2:
				for(int i=0; i<numplugins; i++) {
					slv2_instance_deactivate(instances[i]);
					slv2_instance_free(instances[i]);
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
	cleanup_slv2world:
		slv2_world_free(slv2world);  
	cleanup_listtable:
		arg_freetable(listtable,sizeof(listtable)/sizeof(listtable[0]));

}
