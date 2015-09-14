#ifndef _PNGUTILS_H
#define _PNGUTILS_H

// see comments in (1.2) pngconf.h on Linux
// libpng wants to check that the version of setjmp it uses is the same as the
// one that FSO uses; and it is
#define PNG_SKIP_SETJMP_CHECK

#include "cfile/cfile.h"
#include "globalincs/pstypes.h"
#include "png.h"

#define PNG_ERROR_INVALID		-1
#define PNG_ERROR_NONE 			0
#define PNG_ERROR_READING		1

// reading
extern int png_read_header(const char *real_filename, CFILE *img_cfp = NULL, int *w = 0, int *h = 0, int *bpp = 0, ubyte *palette = NULL);
extern int png_read_bitmap(const char *real_filename, ubyte *image_data, ubyte *bpp, int dest_size, int cf_type = CF_TYPE_ANY);

namespace apng {

class apng_ani {
public:
	struct apng_frame {
		SCP_vector<ubyte>  data;
		SCP_vector<ubyte*> rows;
	};

	apng_frame frame;
	uint       w;
	uint       h;
	uint       bpp;
	uint       nframes;
	uint       current_frame;
	uint       plays;
	float      anim_time;
	float      frame_delay;

	apng_ani(const char* filename);
	~apng_ani();
	int  load_header();
	int  next_frame();
	int  prev_frame();
	void info_callback();
	void row_callback(png_bytep new_row, png_uint_32 row_num);
	void read_unknown_chunks(png_unknown_chunkp chunk);

private:
	struct _chunk_s {
		uint              size;
		SCP_vector<ubyte> data;
	};
	SCP_string              _filename;
	SCP_vector<apng_frame>  _frames;
	apng_frame              _frame_next, _frame_raw;
	SCP_vector<int>         _frame_offsets;
	SCP_vector<ubyte>       _buffer;
	_chunk_s                _chunk_IHDR, _chunk;
	SCP_vector<_chunk_s>    _info_chunks;
	png_byte                _buffer_IHDR[12+13];
	png_byte                _buffer_tRNS[12+256];
	png_byte                _colour_type;
	png_structp             _pngp, _fdat_pngp;
	png_infop               _infop, _fdat_infop;
	CFILE*                  _cfp;
	size_t                  _offset, _image_size;
	uint                    _sequence_num, _id;
	uint                    _framew, _frameh;
	uint                    _x_offset, _y_offset;
	uint                    _color_type;
	uint                    _row_len;
	ushort                  _delay_num, _delay_den;
	ubyte                   _dispose_op, _blend_op;
	bool                    _found_actl, _reading, _got_fcTL, _got_IDAT, _fdat_frame_started;

	uint _read_chunk(_chunk_s& chunk);
	void _process_chunk();
	int  _processing_start(png_unknown_chunkp chunk);
	int  _processing_start();
	void _processing_data(ubyte* data, uint size);
	int  _processing_finish();
	void _apng_failed(const char* msg);
	void _compose_frame();
};

class ApngException : public std::runtime_error
{
	public:
		ApngException(const std::string& msg) : std::runtime_error(msg) {}
		~ApngException() throw() {}
};

}

#endif // _PNGUTILS_H
