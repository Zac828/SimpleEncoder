#define __STDC_CONSTANT_MACROS

// FFMPEG Header Files
extern "C"	
{
#include "libavcodec/avcodec.h"
//#include "libswscale/swscale.h"
#include "libavformat/avformat.h"
#include "libavutil/opt.h"
#include <libavutil/channel_layout.h>
#include <libavutil/common.h>
#include <libavutil/imgutils.h>
#include <libavutil/mathematics.h>
#include <libavutil/samplefmt.h>
}

#include <math.h> 

static int select_sample_rate(AVCodec *codec)
{
    const int *p;
    int best_samplerate = 0;

    if (!codec->supported_samplerates)
        return 44100;

    p = codec->supported_samplerates;
    while (*p) {
        best_samplerate = FFMAX(*p, best_samplerate);
        p++;
    }
    return best_samplerate;
}

static int select_channel_layout(AVCodec *codec)
{
    const uint64_t *p;
    uint64_t best_ch_layout = 0;
    int best_nb_channells   = 0;

    if (!codec->channel_layouts)
        return AV_CH_LAYOUT_STEREO;

    p = codec->channel_layouts;
    while (*p) {
        int nb_channels = av_get_channel_layout_nb_channels(*p);

        if (nb_channels > best_nb_channells) {
            best_ch_layout    = *p;
            best_nb_channells = nb_channels;
        }
        p++;
    }
    return best_ch_layout;
}

static void video_encode_example(const char *filename, AVCodecID codec_id, int p_width, int p_height)
{
    AVCodec *codec;
    AVCodecContext *c= NULL;
    int i, ret, x, y, got_output;
    FILE *f;
    AVFrame *frame;
    AVPacket pkt;
    uint8_t endcode[] = { 0, 0, 1, 0xb7 };

    /* find the mpeg1 video encoder */
    codec = avcodec_find_encoder(codec_id);
    if (!codec) {
        exit(1);
    }

	// Create codec
    c = avcodec_alloc_context3(codec);
    if (!c) {
        exit(1);
    }

    /* put sample parameters */
    c->bit_rate = 4000000;
    /* resolution must be a multiple of two */
    c->width = p_width;
    c->height = p_height;
    /* frames per second */
	AVRational fr = {1000,30000};
    c->time_base = fr;
    /* emit one intra frame every ten frames
     * check frame pict_type before passing frame
     * to encoder, if frame->pict_type is AV_PICTURE_TYPE_I
     * then gop_size is ignored and the output of encoder
     * will always be I frame irrespective to gop_size
     */
    c->gop_size = -1;
    //c->max_b_frames = 1;
    c->pix_fmt = AV_PIX_FMT_YUV420P;

    if (codec_id == AV_CODEC_ID_H264)
	{
        av_opt_set(c->priv_data, "preset", "default", 0);
		av_opt_set(c->priv_data, "profile", "main", 0);
	}

    /* open it */
    if (avcodec_open2(c, codec, NULL) < 0) {
        exit(1);
    }

    f = fopen(filename, "wb");
    if (!f) {
        exit(1);
    }

    frame = av_frame_alloc();
    if (!frame) {
        exit(1);
    }

	frame->format = c->pix_fmt;
    frame->width  = c->width;
    frame->height = c->height;

    /* the image can be allocated by any means and av_image_alloc() is
     * just the most convenient way if av_malloc() is to be used */
    //ret = av_image_alloc(frame->data, frame->linesize, c->width, c->height,
    //                     c->pix_fmt, 32);
	AVPicture dst_picture;
	ret = avpicture_alloc(&dst_picture, c->pix_fmt, c->width, c->height);
    if (ret < 0) {
        exit(1);
    }
	*((AVPicture *)frame) = dst_picture;

    /* encode 1 second of video */
    for (i = 0; i < 250; i++) {
        av_init_packet(&pkt);
        pkt.data = NULL;    // packet data will be allocated by the encoder
        pkt.size = 0;

        fflush(stdout);
        /* prepare a dummy image */
        /* Y */
        for (y = 0; y < c->height; y++) {
            for (x = 0; x < c->width; x++) {
                frame->data[0][y * frame->linesize[0] + x] = x + y + i * 3;
            }
        }

        /* Cb and Cr */
        for (y = 0; y < c->height/2; y++) {
            for (x = 0; x < c->width/2; x++) {
                frame->data[1][y * frame->linesize[1] + x] = 128 + y + i * 2;
                frame->data[2][y * frame->linesize[2] + x] = 64 + x + i * 5;
            }
        }

        frame->pts = i;

        /* encode the image */
        ret = avcodec_encode_video2(c, &pkt, frame, &got_output);
        if (ret < 0) {
            exit(1);
        }

        if (got_output) {
            printf("Write frame %3d (size=%5d)\n", i, pkt.size);
            fwrite(pkt.data, 1, pkt.size, f);
            av_packet_unref(&pkt);
        }
    }

    /* get the delayed frames */
    for (got_output = 1; got_output; i++) {
        fflush(stdout);

        ret = avcodec_encode_video2(c, &pkt, NULL, &got_output);
        if (ret < 0) {
            exit(1);
        }

        if (got_output) {
            printf("Write frame %3d (size=%5d)\n", i, pkt.size);
            fwrite(pkt.data, 1, pkt.size, f);
            av_packet_unref(&pkt);
        }
    }

    /* add sequence end code to have a real mpeg file */
    fwrite(endcode, 1, sizeof(endcode), f);
    fclose(f);

    avcodec_close(c);
    av_free(c);
    av_freep(&frame->data[0]);
    av_frame_free(&frame);
}

int main(int argc, char **argv)
{
	const char *output_type;
	int width = 0, height = 0;

    /* register all the codecs */
    avcodec_register_all();

    if (argc < 2) {
        printf("usage: %s output_type\n"
               "API example program to decode/encode a media stream with libavcodec.\n"
               "This program generates a synthetic stream and encodes it to a file\n"
               "named test.h264, test.mp2 or test.mpg depending on output_type.\n"
               "The encoded stream is then decoded and written to a raw data output.\n"
               "output_type must be chosen between 'h264', 'mp2'.\n",
               argv[0]);
        return 1;
    }
    output_type = argv[1];
	if ( argv[2] != NULL && argv[3] != NULL )
	{
		width = atoi(argv[2]);
		height = atoi(argv[3]);
	}

	if (!strcmp(output_type, "h264")) {
        video_encode_example("test.h264", AV_CODEC_ID_H264, width, height);
	}
	else if (!strcmp(output_type, "mp4")) {
        video_encode_example("test.mp4", AV_CODEC_ID_MPEG4, width, height);
	}
	else
	{
        fprintf(stderr, "Invalid output type '%s', choose between 'h264', or 'mpg'\n",
        output_type);
        return 1;
	}

	return 0;
}