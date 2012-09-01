enum midi_error_code
{
	MIDI_ERR_NO_ERROR=0,
	MIDI_ERR_OPEN_FILE,
	MIDI_ERR_CLOSE_FILE,
	MIDI_ERR_REOPEN_FILE,
	MIDI_ERR_SEEK_TRACK,
	MIDI_ERR_MALFORMED_TEMPO,
	MIDI_ERR_EARLY_END_OF_TRACK,
	MIDI_ERR_MISALIGNED_TRACK,
	MIDI_ERR_CORRUPTED_FILE_HEADER,
	MIDI_ERR_CORRUPTED_TRACK_HEADER,
	MIDI_ERR_READING_EVENT,
	MIDI_ERR_WRITING_EVENT,
	MIDI_ERR_OUT_OF_MEMORY,
	MIDI_ERR_FINALIZING,
	MIDI_ERR_WRITING_HEADER
	MIDI_MAX_ERROR
};


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



