//Taken from libsndfile


typedef struct
{	int 		error ;
	const char	*str ;
	bool ferr;
} ErrorStruct ;

static
ErrorStruct MIDIErrors [] =
{
	{MIDI_ERR_NO_ERROR,			"No Error",						false},
	{MIDI_ERR_OPEN_FILE, 			"IO Error opening file",				true },
	{MIDI_ERR_CLOSE_FILE, 			"IO Error closing file",				true },
	{MIDI_ERR_REOPEN_FILE,			"IO Error opening file for reading multiple times: ",	true },
	{MIDI_ERR_SEEK_TRACK, 			"IO Error Seeking to MIDI track",			true },
	{MIDI_ERR_MALFORMED_TEMPO,		"Error, malformed Tempo event (corrupted midi file)",	false},
	{MIDI_ERR_EARLY_END_OF_TRACK,		"Error, early End-Of-Track event (corrupted midi file)",false},
	{MIDI_ERR_MISALIGNED_TRACK,		"Error, misaligned MIDI track (corrupted midi file)",	false},
	{MIDI_ERR_CORRUPTED_FILE_HEADER,	"Error, corrupted MIDI file header",			false},
	{MIDI_ERR_CORRUPTED_TRACK_HEADER,	"Error, corrupted MIDI track header",			false},
	{MIDI_ERR_READING_EVENT,		"IO Error reading MIDI events",			true },
	{MIDI_ERR_WRITING_EVENT,		"IO Error writing MIDI event",			true },
	{MIDI_ERR_OUT_OF_MEMORY,		"Error, out of memory",					false},
	{MIDI_ERR_FINALIZING,			"IO Error finalizing MIDI file",			true },
	{MIDI_ERR_WRITING_HEADER,		"IO Error writing MIDI file header",			true }
};

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


