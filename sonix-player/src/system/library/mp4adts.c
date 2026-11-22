#include "mp4adts.h"

#include <string.h>

static uint32_t be32(const uint8_t *p) {
	return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static uint64_t be64(const uint8_t *p) { return ((uint64_t)be32(p) << 32) | be32(p + 4); }

// One box: where its payload starts and ends. False when it runs past `end`.
typedef struct {
	char type[5];
	size_t start;	// the box header
	size_t payload; // first byte after the header
	size_t end;		// one past the last byte
} box_t;

static bool read_box(const uint8_t *d, size_t at, size_t end, box_t *b) {
	if (at + 8 > end) {
		return false;
	}
	uint64_t size = be32(d + at);
	size_t header = 8;
	memcpy(b->type, d + at + 4, 4);
	b->type[4] = '\0';
	if (size == 1) {
		if (at + 16 > end) {
			return false;
		}
		size = be64(d + at + 8);
		header = 16;
	} else if (size == 0) {
		size = end - at; // to the end of the enclosing space
	}
	if (size < header || size > end - at) {
		return false;
	}
	b->start = at;
	b->payload = at + header;
	b->end = at + (size_t)size;
	return true;
}

// The first box of `type` directly inside [from, end). False when absent.
static bool find_child(const uint8_t *d, size_t from, size_t end, const char *type, box_t *out) {
	size_t at = from;
	box_t b;
	while (read_box(d, at, end, &b)) {
		if (memcmp(b.type, type, 4) == 0) {
			*out = b;
			return true;
		}
		at = b.end;
	}
	return false;
}

// --- the codec config -------------------------------------------------------

typedef struct {
	int object_type; // 2 = AAC-LC
	int freq_index;
	int channels;
} asc_t;

// An MPEG-4 descriptor length: up to four bytes, seven bits each.
static bool desc_len(const uint8_t *d, size_t *at, size_t end, size_t *len) {
	*len = 0;
	for (int i = 0; i < 4; i++) {
		if (*at >= end) {
			return false;
		}
		uint8_t c = d[(*at)++];
		*len = (*len << 7) | (c & 0x7F);
		if (!(c & 0x80)) {
			return true;
		}
	}
	return true;
}

// Walks esds -> ES_Descriptor (3) -> DecoderConfigDescriptor (4) ->
// DecoderSpecificInfo (5), whose first two bytes are the AudioSpecificConfig.
static bool parse_esds(const uint8_t *d, size_t at, size_t end, asc_t *asc) {
	at += 4; // version and flags
	while (at < end) {
		uint8_t tag = d[at++];
		size_t len;
		if (!desc_len(d, &at, end, &len) || len > end - at) {
			return false;
		}
		if (tag == 0x03) {
			// ES_ID (2) and flags (1), then optional fields named by the flags.
			if (len < 3) {
				return false;
			}
			uint8_t flags = d[at + 2];
			size_t skip = 3;
			if (flags & 0x80) {
				skip += 2;
			}
			if (flags & 0x40) {
				if (at + skip >= end) {
					return false;
				}
				skip += 1 + d[at + skip];
			}
			if (flags & 0x20) {
				skip += 2;
			}
			at += skip;
			continue; // descend
		}
		if (tag == 0x04) {
			at += 13; // object type, stream type, buffer size, bitrates
			continue; // descend
		}
		if (tag == 0x05) {
			if (len < 2) {
				return false;
			}
			int ot = d[at] >> 3;
			int fi = ((d[at] & 0x07) << 1) | (d[at + 1] >> 7);
			int ch = (d[at + 1] >> 3) & 0x0F;
			if (ot == 31 || fi == 15) {
				return false; // escape values: not something ADTS can carry
			}
			asc->object_type = ot;
			asc->freq_index = fi;
			asc->channels = ch;
			return true;
		}
		at += len;
	}
	return false;
}

// moov/trak/mdia/minf/stbl/stsd/mp4a/esds, taking the first audio track.
static bool find_asc(const uint8_t *d, const box_t *moov, asc_t *asc) {
	size_t at = moov->payload;
	box_t trak;
	while (read_box(d, at, moov->end, &trak)) {
		at = trak.end;
		if (memcmp(trak.type, "trak", 4) != 0) {
			continue;
		}
		box_t mdia, minf, stbl, stsd;
		if (!find_child(d, trak.payload, trak.end, "mdia", &mdia) ||
			!find_child(d, mdia.payload, mdia.end, "minf", &minf) ||
			!find_child(d, minf.payload, minf.end, "stbl", &stbl) ||
			!find_child(d, stbl.payload, stbl.end, "stsd", &stsd)) {
			continue;
		}
		// stsd: version/flags (4), entry count (4), then sample entries.
		box_t entry;
		if (!read_box(d, stsd.payload + 8, stsd.end, &entry) || memcmp(entry.type, "mp4a", 4) != 0) {
			continue;
		}
		// AudioSampleEntry: 6 reserved + 2 index + 8 reserved + 2 channels +
		// 2 sample size + 4 reserved + 4 rate = 28 bytes before child boxes.
		box_t esds;
		if (find_child(d, entry.payload + 28, entry.end, "esds", &esds) &&
			parse_esds(d, esds.payload, esds.end, asc)) {
			return true;
		}
	}
	return false;
}

// --- writing -----------------------------------------------------------------

static bool write_frame(FILE *out, const asc_t *asc, const uint8_t *frame, size_t len) {
	size_t total = len + 7;
	if (total > 0x1FFF) {
		return false;
	}
	int profile = asc->object_type - 1; // ADTS stores object type minus one
	if (profile < 0 || profile > 3) {
		profile = 1; // LC
	}
	uint8_t h[7];
	h[0] = 0xFF;
	h[1] = 0xF1; // MPEG-4, layer 0, no CRC
	h[2] = (uint8_t)((profile << 6) | (asc->freq_index << 2) | ((asc->channels >> 2) & 0x01));
	h[3] = (uint8_t)(((asc->channels & 0x03) << 6) | ((total >> 11) & 0x03));
	h[4] = (uint8_t)((total >> 3) & 0xFF);
	h[5] = (uint8_t)(((total & 0x07) << 5) | 0x1F);
	h[6] = 0xFC;
	return fwrite(h, 1, 7, out) == 7 && fwrite(frame, 1, len, out) == len;
}

// One moof: its traf's trun lists the sample sizes, and the samples sit at
// base + data_offset, base being the moof itself (default-base-is-moof, which
// is what DASH segments use; with an explicit base the explicit one wins).
static mp4adts_result_t do_moof(const uint8_t *d, size_t size, const box_t *moof, uint32_t trex_size,
								const asc_t *asc, FILE *out, long *frames) {
	size_t at = moof->payload;
	box_t traf;
	while (read_box(d, at, moof->end, &traf)) {
		at = traf.end;
		if (memcmp(traf.type, "traf", 4) != 0) {
			continue;
		}
		uint64_t base = moof->start;
		uint32_t default_size = trex_size;

		box_t tfhd;
		if (find_child(d, traf.payload, traf.end, "tfhd", &tfhd)) {
			size_t p = tfhd.payload;
			if (p + 8 > tfhd.end) {
				return MP4ADTS_BROKEN;
			}
			uint32_t flags = be32(d + p) & 0xFFFFFF;
			p += 8; // version/flags, track id
			if (flags & 0x01) {
				if (p + 8 > tfhd.end) {
					return MP4ADTS_BROKEN;
				}
				base = be64(d + p);
				p += 8;
			}
			if (flags & 0x02) {
				p += 4;
			}
			if (flags & 0x08) {
				p += 4;
			}
			if (flags & 0x10) {
				if (p + 4 > tfhd.end) {
					return MP4ADTS_BROKEN;
				}
				default_size = be32(d + p);
			}
		}

		size_t tat = traf.payload;
		box_t trun;
		while (read_box(d, tat, traf.end, &trun)) {
			tat = trun.end;
			if (memcmp(trun.type, "trun", 4) != 0) {
				continue;
			}
			size_t p = trun.payload;
			if (p + 8 > trun.end) {
				return MP4ADTS_BROKEN;
			}
			uint32_t flags = be32(d + p) & 0xFFFFFF;
			uint32_t count = be32(d + p + 4);
			p += 8;
			int64_t data_offset = 0;
			if (flags & 0x01) {
				data_offset = (int32_t)be32(d + p);
				p += 4;
			}
			if (flags & 0x04) {
				p += 4;
			}
			uint64_t pos = base + (uint64_t)data_offset;
			for (uint32_t i = 0; i < count; i++) {
				if (flags & 0x100) {
					p += 4;
				}
				uint32_t sample = default_size;
				if (flags & 0x200) {
					if (p + 4 > trun.end) {
						return MP4ADTS_BROKEN;
					}
					sample = be32(d + p);
					p += 4;
				}
				if (flags & 0x400) {
					p += 4;
				}
				if (flags & 0x800) {
					p += 4;
				}
				if (p > trun.end || sample == 0 || pos + sample > size) {
					return MP4ADTS_BROKEN;
				}
				if (!write_frame(out, asc, d + pos, sample)) {
					return MP4ADTS_WRITE_FAILED;
				}
				pos += sample;
				(*frames)++;
			}
		}
	}
	return MP4ADTS_OK;
}

mp4adts_result_t mp4adts_convert(const uint8_t *d, size_t size, FILE *out, long *frames_out) {
	long frames = 0;
	if (frames_out) {
		*frames_out = 0;
	}

	box_t moov;
	if (!find_child(d, 0, size, "moov", &moov)) {
		return MP4ADTS_BROKEN;
	}
	asc_t asc;
	if (!find_asc(d, &moov, &asc)) {
		return MP4ADTS_NOT_AAC;
	}

	// trex may carry the default sample size the fragments then leave out.
	uint32_t trex_size = 0;
	box_t mvex, trex;
	if (find_child(d, moov.payload, moov.end, "mvex", &mvex) &&
		find_child(d, mvex.payload, mvex.end, "trex", &trex) && trex.payload + 24 <= trex.end) {
		trex_size = be32(d + trex.payload + 16);
	}

	bool any_moof = false;
	size_t at = 0;
	box_t b;
	while (read_box(d, at, size, &b)) {
		at = b.end;
		if (memcmp(b.type, "moof", 4) != 0) {
			continue;
		}
		any_moof = true;
		mp4adts_result_t r = do_moof(d, size, &b, trex_size, &asc, out, &frames);
		if (r != MP4ADTS_OK) {
			return r;
		}
	}
	if (!any_moof) {
		return MP4ADTS_NOT_FRAGMENTED;
	}
	if (frames_out) {
		*frames_out = frames;
	}
	return frames > 0 ? MP4ADTS_OK : MP4ADTS_BROKEN;
}
