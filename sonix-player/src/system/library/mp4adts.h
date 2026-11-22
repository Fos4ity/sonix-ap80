#ifndef MP4ADTS_H
#define MP4ADTS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

// Fragmented MP4 holding AAC -> raw AAC with ADTS headers.
//
// YouTube's audio-only AAC stream (itag 140) is a DASH file: ftyp, a moov with
// no sample tables, a sidx, then dozens of moof+mdat pairs. mp4.c reads the
// sample tables of an ordinary MP4 and has nothing to read here, while the
// ADTS reader in decode.c plays a bare sequence of frames happily. So the
// frames are lifted out of the fragments and each given the seven-byte header
// that says how long it is. No audio is touched: the bytes the decoder sees are
// exactly the ones YouTube sent.
//
// Works on a buffer already in memory (a song is a few megabytes).

typedef enum {
	MP4ADTS_OK = 0,
	MP4ADTS_NOT_FRAGMENTED, // an ordinary MP4: keep it as .m4a, mp4.c reads it
	MP4ADTS_NOT_AAC,		// no mp4a sample entry, or a config ADTS cannot express
	MP4ADTS_BROKEN,			// boxes that do not add up
	MP4ADTS_WRITE_FAILED,
} mp4adts_result_t;

// Writes the ADTS stream to `out`. `frames_out`, when not NULL, receives how
// many frames were written.
mp4adts_result_t mp4adts_convert(const uint8_t *data, size_t size, FILE *out, long *frames_out);

#endif /* MP4ADTS_H */
