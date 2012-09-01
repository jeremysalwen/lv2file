//Taken from libsndfile
void print_midi_error	(int errnum)
{	static const char *bad_errnum ="No error defined for this error number. This is a bug in lv2file." ;
	int	k;

	if (errnum == MIDI_MAX_ERROR)
		fprintf(stderr, "%s\n",MIDIErrors [0].str);

	if (errnum < 0 || errnum > SFE_MAX_ERROR)
	{	/* This really shouldn't happen in release versions. */
		printf ("Not a valid error number (%d).\n", errnum) ;
		fprintf(stderr, "%s\n",bad_errnum);
	} ;

	for (k = 0 ; MIDIErrors [k].str ; k++) {
		if (errnum == MIDIErrors[k].error) {
			if(MIDIErrors[k].ferr) {
				perror(MIDIErrors[k].str);
 			} else {
				fprintf(stderr,"%s\n",MIDIErrors[k].str);	
			}
			return;
		}
	}

	fprintf(stderr, "%s\n",bad_errnum);
} /* sf_error_number */

