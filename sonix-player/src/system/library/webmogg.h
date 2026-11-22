#ifndef WEBMOGG_H
#define WEBMOGG_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

// WebM holding Opus -> Ogg Opus.
//
// YouTube's better free audio stream (itag 251, Opus around 130-160 kbps) is
// a WebM file: an EBML header, a Segment with the track description (whose
// CodecPrivate is the OpusHead itself), a Cues index and clusters of blocks,
// one Opus packet per block. opusdec.c reads Ogg Opus through libopusfile and
// knows nothing of Matroska. So the packets are lifted out of the blocks and
// laid into Ogg pages, with the granule positions Ogg needs counted from the
// packets' own TOC bytes. No audio is touched: the decoder gets exactly the
// packets YouTube sent.
//
// Works on a buffer already in memory (a song is a few megabytes).

typedef enum {
	WEBMOGG_OK = 0,
	WEBMOGG_NOT_OPUS, // no A_OPUS track, or no OpusHead in it
	WEBMOGG_BROKEN,	  // elements that do not add up, or lacing it does not handle
	WEBMOGG_WRITE_FAILED,
} webmogg_result_t;

// Writes the Ogg stream to `out`. `packets_out`, when not NULL, receives how
// many audio packets were written.
webmogg_result_t webmogg_convert(const uint8_t *data, size_t size, FILE *out, long *packets_out);

#endif /* WEBMOGG_H */
