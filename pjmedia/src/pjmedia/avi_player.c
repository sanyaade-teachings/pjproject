/* 
 * Copyright (C) 2008-2011 Teluu Inc. (http://www.teluu.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA 
 */

/**
 * Default file player/writer buffer size.
 */
#include <pjmedia/avi_stream.h>
#include <pjmedia/alaw_ulaw.h>
#include <pjmedia/av_sync.h>
#include <pjmedia/avi.h>
#include <pjmedia/errno.h>
#include <pjmedia/wave.h>
#include <pj/assert.h>
#include <pj/ctype.h>
#include <pj/file_access.h>
#include <pj/file_io.h>
#include <pj/log.h>
#include <pj/pool.h>
#include <pj/string.h>


#if defined(PJMEDIA_HAS_VIDEO) && (PJMEDIA_HAS_VIDEO != 0)


#define THIS_FILE   "avi_player.c"

#define AVIF_MUSTUSEINDEX       0x00000020
#define AVIF_ISINTERLEAVED      0x00000100
#define AVISF_DISABLED          0x00000001
#define AVISF_VIDEO_PALCHANGES  0x00010000

#define AVI_EOF (int)0xFFEEFFEE

//#define COMPARE_TAG(doc_tag, tag) (doc_tag==*((pj_uint32_t*)avi_tags[tag]))
#define COMPARE_TAG(doc_tag, tag) \
            (pj_memcmp(&(doc_tag), &avi_tags[tag], 4)==0)

#define SIGNATURE           PJMEDIA_SIG_PORT_VID_AVI_PLAYER

#define VIDEO_CLOCK_RATE        90000

#if 0
#   define TRACE_(x)    PJ_LOG(4,x)
#else
#   define TRACE_(x)
#endif

#define data_to_host pjmedia_avi_swap_data
#define data_to_host2 pjmedia_avi_swap_data2

typedef struct avi_fmt_info
{
    pjmedia_format_id   fmt_id;
    pjmedia_format_id   eff_fmt_id;
} avi_fmt_info;

static avi_fmt_info avi_fmts[] =
{
    {PJMEDIA_FORMAT_MJPEG}, {PJMEDIA_FORMAT_H264},
    {PJMEDIA_FORMAT_UYVY}, {PJMEDIA_FORMAT_YUY2},
    {PJMEDIA_FORMAT_IYUV}, {PJMEDIA_FORMAT_I420},
    {PJMEDIA_FORMAT_DIB}, {PJMEDIA_FORMAT_RGB24},
    {PJMEDIA_FORMAT_RGB32},
    {PJMEDIA_FORMAT_PACK('X','V','I','D'), PJMEDIA_FORMAT_MPEG4},
    {PJMEDIA_FORMAT_PACK('x','v','i','d'), PJMEDIA_FORMAT_MPEG4},
    {PJMEDIA_FORMAT_PACK('D','I','V','X'), PJMEDIA_FORMAT_MPEG4},
    {PJMEDIA_FORMAT_PACK('F','M','P','4'), PJMEDIA_FORMAT_MPEG4},
    {PJMEDIA_FORMAT_PACK('D','X','5','0'), PJMEDIA_FORMAT_MPEG4}
};

typedef struct avi_reader_streams
{
    pjmedia_avi_streams base;

    /* AV synchronization */
    pjmedia_av_sync *avsync;
    pj_size_t        eof_cnt;
} avi_reader_streams;

struct avi_reader_port
{
    pjmedia_port     base;
    unsigned         stream_id;
    unsigned         options;
    pjmedia_format_id fmt_id;
    pj_uint16_t      bits_per_sample;
    pj_bool_t        eof;
    pj_off_t         fsize;
    pj_off_t         start_data;
    pj_uint8_t       pad;
    pj_oshandle_t    fd;
    pj_ssize_t       size_left;

    pj_size_t        frame_cnt;
    pj_timestamp     next_ts;

    /* AV synchronization */
    pjmedia_av_sync_media *avsync_media;
    pj_size_t        slow_down_frm;
    avi_reader_streams *avi_streams;

    pj_status_t    (*cb)(pjmedia_port*, void*);
    pj_bool_t        subscribed;
    void           (*cb2)(pjmedia_port*, void*);
};

static pj_status_t avi_get_frame(pjmedia_port *this_port, 
                                 pjmedia_frame *frame);
static pj_status_t avi_on_destroy(pjmedia_port *this_port);

static struct avi_reader_port *create_avi_port(pj_pool_t *pool,
                                               pj_grp_lock_t *grp_lock)
{
    const pj_str_t name = pj_str("file");
    struct avi_reader_port *port;

    port = PJ_POOL_ZALLOC_T(pool, struct avi_reader_port);
    if (!port)
        return NULL;

    /* Put in default values.
     * These will be overriden once the file is read.
     */
    pjmedia_port_info_init(&port->base.info, &name, SIGNATURE, 
                           8000, 1, 16, 80);

    port->fd = (pj_oshandle_t)(pj_ssize_t)-1;
    port->base.get_frame = &avi_get_frame;
    port->base.on_destroy = &avi_on_destroy;

    pjmedia_port_init_grp_lock(&port->base, pool, grp_lock);

    return port;
}

#define file_read(fd, data, size) file_read2(fd, data, size, 32)
#define file_read2(fd, data, size, bits) file_read3(fd, data, size, bits, NULL)

static pj_status_t file_read3(pj_oshandle_t fd, void *data, pj_ssize_t size,
                              pj_uint16_t bits, pj_ssize_t *psz_read)
{
    pj_ssize_t size_read = size, size_to_read = size;
    pj_status_t status = pj_file_read(fd, data, &size_read);
    if (status != PJ_SUCCESS)
        return status;

    /* Normalize AVI header fields values from little-endian to host
     * byte order.
     */
    if (bits > 0) {
        data_to_host(data, bits, size_read);
    }

    if (size_read != size_to_read) {
        if (psz_read)
            *psz_read = size_read;
        return AVI_EOF;
    }

    return status;
}


static void streams_on_destroy(void *arg)
{
    avi_reader_streams *streams = (avi_reader_streams *)arg;

    if (streams->avsync)
        pjmedia_av_sync_destroy(streams->avsync);
    pj_pool_safe_release(&streams->base.pool);
}


/* Get filename from path */
static const char *get_fname(const char *path)
{
    pj_size_t len = pj_ansi_strlen(path);
    const char *p = path + len - 1;

    while (p > path) {
        if (*p == '\\' || *p == '/' || *p == ':')
            return p + 1;
        --p;
    }
    return p;
}

/*
 * Create AVI player port.
 */
PJ_DEF(pj_status_t)
pjmedia_avi_player_create_streams(pj_pool_t *pool_,
                                  const char *filename,
                                  unsigned options,
                                  pjmedia_avi_streams **p_streams)
{
    avi_reader_streams *streams = NULL;
    pjmedia_avi_hdr avi_hdr;
    struct avi_reader_port *fport[PJMEDIA_AVI_MAX_NUM_STREAMS];
    pj_off_t pos;
    unsigned i, nstr = 0;
    pj_pool_t *pool = NULL;
    pj_grp_lock_t *grp_lock = NULL;
    pj_status_t status = PJ_SUCCESS;

    /* Check arguments. */
    PJ_ASSERT_RETURN(pool_ && filename && p_streams, PJ_EINVAL);

    /* Check the file really exists. */
    if (!pj_file_exists(filename)) {
        return PJ_ENOTFOUND;
    }

    /* Create own pool */
    pool = pj_pool_create(pool_->factory, "aviplayer", 500, 500, NULL);
    PJ_ASSERT_RETURN(pool, PJ_ENOMEM);

    /* Create group lock */
    status = pj_grp_lock_create(pool, NULL, &grp_lock);
    if (status != PJ_SUCCESS)
        goto on_error;

    /* Create fport instance. */
    fport[0] = create_avi_port(pool, grp_lock);
    if (!fport[0]) {
        /* Destroy group lock here to avoid leak */
        pj_grp_lock_destroy(grp_lock);
        grp_lock = NULL;

        status = PJ_ENOMEM;
        goto on_error;
    }

    /* Get the file size. */
    fport[0]->fsize = pj_file_size(filename);

    /* Size must be more than AVI header size */
    if (fport[0]->fsize <= (pj_off_t)(sizeof(riff_hdr_t) + sizeof(avih_hdr_t) +
                                      sizeof(strl_hdr_t)))
    {
        status = PJMEDIA_EINVALIMEDIATYPE;
        goto on_error;
    }

    /* Open file. */
    status = pj_file_open(pool, filename, PJ_O_RDONLY | PJ_O_CLOEXEC,
                          &fport[0]->fd);
    if (status != PJ_SUCCESS)
        goto on_error;

    /* Read the RIFF + AVIH header. */
    status = file_read(fport[0]->fd, &avi_hdr,
                       sizeof(riff_hdr_t) + sizeof(avih_hdr_t));
    if (status != PJ_SUCCESS)
        goto on_error;

    /* Validate AVI file. */
    if (!COMPARE_TAG(avi_hdr.riff_hdr.riff, PJMEDIA_AVI_RIFF_TAG) ||
        !COMPARE_TAG(avi_hdr.riff_hdr.avi, PJMEDIA_AVI_AVI_TAG) ||
        !COMPARE_TAG(avi_hdr.avih_hdr.list_tag, PJMEDIA_AVI_LIST_TAG) ||
        !COMPARE_TAG(avi_hdr.avih_hdr.hdrl_tag, PJMEDIA_AVI_HDRL_TAG) ||
        !COMPARE_TAG(avi_hdr.avih_hdr.avih, PJMEDIA_AVI_AVIH_TAG))
    {
        status = PJMEDIA_EINVALIMEDIATYPE;
        goto on_error;
    }

    PJ_LOG(5, (THIS_FILE, "The AVI file has %d streams.",
               avi_hdr.avih_hdr.num_streams));

    /* Unsupported AVI format. */
    if (avi_hdr.avih_hdr.num_streams > PJMEDIA_AVI_MAX_NUM_STREAMS) {
        status = PJMEDIA_EAVIUNSUPP;
        goto on_error;
    }

    /** 
     * TODO: Possibly unsupported AVI format.
     * If you encounter this warning, verify whether the avi player
     * is working properly.
     */
    if (avi_hdr.avih_hdr.flags & AVIF_MUSTUSEINDEX ||
        avi_hdr.avih_hdr.pad > 1)
    {
        PJ_LOG(3, (THIS_FILE, "Warning!!! Possibly unsupported AVI format: "
                   "flags:%d, pad:%d", avi_hdr.avih_hdr.flags, 
                   avi_hdr.avih_hdr.pad));
    }

    /* Read the headers of each stream. */
    for (i = 0; i < avi_hdr.avih_hdr.num_streams; i++) {
        pj_size_t elem = 0;
        pj_off_t size_to_read;

        /* Read strl header */
        status = file_read(fport[0]->fd, &avi_hdr.strl_hdr[i],
                           sizeof(strl_hdr_t));
        if (status != PJ_SUCCESS)
            goto on_error;
        
        elem = COMPARE_TAG(avi_hdr.strl_hdr[i].data_type, 
                           PJMEDIA_AVI_VIDS_TAG) ? 
               sizeof(strf_video_hdr_t) :
               COMPARE_TAG(avi_hdr.strl_hdr[i].data_type, 
                           PJMEDIA_AVI_AUDS_TAG) ?
               sizeof(strf_audio_hdr_t) : 0;

        /* Read strf header */
        status = file_read2(fport[0]->fd, &avi_hdr.strf_hdr[i],
                            elem, 0);
        if (status != PJ_SUCCESS)
            goto on_error;

        /* Normalize the endian */
        if (elem == sizeof(strf_video_hdr_t)) {
            data_to_host2(&avi_hdr.strf_hdr[i],
                          PJ_ARRAY_SIZE(strf_video_hdr_sizes),
                          strf_video_hdr_sizes);
        } else if (elem == sizeof(strf_audio_hdr_t)) {
            data_to_host2(&avi_hdr.strf_hdr[i],
                          PJ_ARRAY_SIZE(strf_audio_hdr_sizes),
                          strf_audio_hdr_sizes);
        }

        /* Skip the remainder of the header */
        size_to_read = avi_hdr.strl_hdr[i].list_sz - (sizeof(strl_hdr_t) -
                       8) - elem;
        status = pj_file_setpos(fport[0]->fd, size_to_read, PJ_SEEK_CUR);
        if (status != PJ_SUCCESS) {
            goto on_error;
        }
    }

    /* Finish reading the AVIH header */
    status = pj_file_setpos(fport[0]->fd, avi_hdr.avih_hdr.list_sz +
                            sizeof(riff_hdr_t) + 8, PJ_SEEK_SET);
    if (status != PJ_SUCCESS) {
        goto on_error;
    }

    /* Skip any JUNK or LIST INFO until we get MOVI tag */
    do {
        pjmedia_avi_subchunk ch;
        int read = 0;
        pj_off_t size_to_read;

        status = file_read(fport[0]->fd, &ch, sizeof(pjmedia_avi_subchunk));
        if (status != PJ_SUCCESS) {
            goto on_error;
        }

        if (COMPARE_TAG(ch.id, PJMEDIA_AVI_LIST_TAG))
        {
            read = 4;
            status = file_read(fport[0]->fd, &ch, read);
            if (status != PJ_SUCCESS) {
                goto on_error;
            }

            if (COMPARE_TAG(ch.id, PJMEDIA_AVI_MOVI_TAG))
                break;
        }

        if (ch.len < (pj_uint32_t)read) {
            status = PJ_EINVAL;
            goto on_error;
        }
        PJ_CHECK_OVERFLOW_UINT32_TO_LONG(ch.len - read, 
                                         status = PJ_EINVAL; goto on_error;);
        size_to_read = (pj_off_t)ch.len - read;

        status = pj_file_setpos(fport[0]->fd, size_to_read, PJ_SEEK_CUR);
        if (status != PJ_SUCCESS) {
            goto on_error;
        }
    } while(1);

    status = pj_file_getpos(fport[0]->fd, &pos);
    if (status != PJ_SUCCESS)
        goto on_error;

    for (i = 0, nstr = 0; i < avi_hdr.avih_hdr.num_streams; i++) {
        pjmedia_format_id fmt_id;

        /* Skip non-audio, non-video, or disabled streams) */
        if ((!COMPARE_TAG(avi_hdr.strl_hdr[i].data_type, 
                          PJMEDIA_AVI_VIDS_TAG) &&
             !COMPARE_TAG(avi_hdr.strl_hdr[i].data_type, 
                          PJMEDIA_AVI_AUDS_TAG)) ||
            avi_hdr.strl_hdr[i].flags & AVISF_DISABLED)
        {
            continue;
        }

        if (COMPARE_TAG(avi_hdr.strl_hdr[i].data_type, 
                        PJMEDIA_AVI_VIDS_TAG))
        {
            int j;

            if (avi_hdr.strl_hdr[i].flags & AVISF_VIDEO_PALCHANGES) {
                PJ_LOG(4, (THIS_FILE, "Unsupported video stream"));
                continue;
            }

            fmt_id = avi_hdr.strl_hdr[i].codec;
            for (j = (int)PJ_ARRAY_SIZE(avi_fmts)-1; j >= 0; j--) {
                /* Check supported video formats here */
                if (fmt_id == avi_fmts[j].fmt_id) {
                    if (avi_fmts[j].eff_fmt_id)
                        fmt_id = avi_fmts[j].eff_fmt_id;
                    break;
                }
            }
            
            if (j < 0) {
                PJ_LOG(4, (THIS_FILE, "Unsupported video stream"));
                continue;
            }
        } else {
            /* Check supported audio formats here */
            strf_audio_hdr_t *hdr = (strf_audio_hdr_t*)
                                    &avi_hdr.strf_hdr[i].strf_audio_hdr;
            if (hdr->fmt_tag == PJMEDIA_WAVE_FMT_TAG_PCM &&
                hdr->bits_per_sample == 16)
            {
                fmt_id = PJMEDIA_FORMAT_PCM;
            }
            else if (hdr->fmt_tag == PJMEDIA_WAVE_FMT_TAG_ALAW)
            {
                fmt_id = PJMEDIA_FORMAT_PCMA;
            }
            else if (hdr->fmt_tag == PJMEDIA_WAVE_FMT_TAG_ULAW)
            {
                fmt_id = PJMEDIA_FORMAT_PCMU;
            }
            else
            {
                PJ_LOG(4, (THIS_FILE, "Unsupported audio stream"));
                continue;
            }
        }

        if (nstr > 0) {
            /* Create fport instance. */
            fport[nstr] = create_avi_port(pool, grp_lock);
            if (!fport[nstr]) {
                status = PJ_ENOMEM;
                goto on_error;
            }

            /* Open file. */
            status = pj_file_open(pool, filename, PJ_O_RDONLY | PJ_O_CLOEXEC,
                                  &fport[nstr]->fd);
            if (status != PJ_SUCCESS)
                goto on_error;

            /* Set the file position */
            status = pj_file_setpos(fport[nstr]->fd, pos, PJ_SEEK_SET);
            if (status != PJ_SUCCESS) {
                goto on_error;
            }
        }

        fport[nstr]->stream_id = i;
        fport[nstr]->fmt_id = fmt_id;

        nstr++;
    }

    if (nstr == 0) {
        status = PJMEDIA_EAVIUNSUPP;
        goto on_error;
    }

    for (i = 0; i < nstr; i++) {
        strl_hdr_t *strl_hdr = &avi_hdr.strl_hdr[fport[i]->stream_id];
        char port_name[PJ_MAX_OBJ_NAME];

        /* Initialize */
        fport[i]->options = options;
        fport[i]->fsize = fport[0]->fsize;
        /* Current file position now points to start of data */
        fport[i]->start_data = pos;
        
        if (COMPARE_TAG(strl_hdr->data_type, PJMEDIA_AVI_VIDS_TAG)) {
            strf_video_hdr_t *strf_hdr =
                &avi_hdr.strf_hdr[fport[i]->stream_id].strf_video_hdr;
            const pjmedia_video_format_info *vfi;

            vfi = pjmedia_get_video_format_info(
                pjmedia_video_format_mgr_instance(),
                strl_hdr->codec);

            fport[i]->bits_per_sample = (vfi ? vfi->bpp : 0);
            //fport[i]->usec_per_frame = avi_hdr.avih_hdr.usec_per_frame;
            pjmedia_format_init_video(&fport[i]->base.info.fmt,
                                      fport[i]->fmt_id,
                                      strf_hdr->biWidth,
                                      strf_hdr->biHeight,
                                      strl_hdr->rate,
                                      strl_hdr->scale);
#if 0
            /* The calculation below is wrong. strf_hdr->biSizeImage shows
             * uncompressed size. Looks like we need to go the ugly way to
             * get the bitrage:
             *    http://www.virtualdub.org/blog/pivot/entry.php?id=159
             */
            bps = strf_hdr->biSizeImage * 8 * strl_hdr->rate / strl_hdr->scale;
            if (bps==0) {
                /* strf_hdr->biSizeImage may be zero for uncompressed RGB */
                bps = strf_hdr->biWidth * strf_hdr->biHeight *
                        strf_hdr->biBitCount *
                        strl_hdr->rate / strl_hdr->scale;
            }
            fport[i]->base.info.fmt.det.vid.avg_bps = bps;
            fport[i]->base.info.fmt.det.vid.max_bps = bps;
#endif
        } else {
            strf_audio_hdr_t *strf_hdr =
                &avi_hdr.strf_hdr[fport[i]->stream_id].strf_audio_hdr;

            fport[i]->bits_per_sample = strf_hdr->bits_per_sample;
            //fport[i]->usec_per_frame = avi_hdr.avih_hdr.usec_per_frame;
            pjmedia_format_init_audio(&fport[i]->base.info.fmt,
                                      fport[i]->fmt_id,
                                      strf_hdr->sample_rate,
                                      strf_hdr->nchannels,
                                      strf_hdr->bits_per_sample,
                                      20000 /* fport[i]->usec_per_frame */,
                                      strf_hdr->bytes_per_sec * 8,
                                      strf_hdr->bytes_per_sec * 8);

            /* Set format to PCM (we will decode PCMA/U) */
            if (fport[i]->fmt_id == PJMEDIA_FORMAT_PCMA ||
                fport[i]->fmt_id == PJMEDIA_FORMAT_PCMU)
            {
                fport[i]->base.info.fmt.id = PJMEDIA_FORMAT_PCM;
                fport[i]->base.info.fmt.det.aud.bits_per_sample = 16;
            }
        }

        pj_ansi_snprintf(port_name, sizeof(port_name), "%s-of-%s",
                         pjmedia_type_name(fport[i]->base.info.fmt.type),
                         get_fname(filename));
        pj_strdup2_with_null(pool, &fport[i]->base.info.name, port_name);
    }

    /* Done. */
    streams = pj_pool_calloc(pool, 1, sizeof(avi_reader_streams));
    *p_streams = (pjmedia_avi_streams *)streams;
    (*p_streams)->num_streams = nstr;
    (*p_streams)->streams = pj_pool_calloc(pool, (*p_streams)->num_streams,
                                           sizeof(pjmedia_port *));
    for (i = 0; i < nstr; i++)
        (*p_streams)->streams[i] = &fport[i]->base;

    /* Create AV synchronizer, if not disabled */
    if ((options & PJMEDIA_AVI_FILE_NO_SYNC) == 0) {
        pjmedia_av_sync *avsync;
        pj_timestamp ts_zero = {{0}};
        pjmedia_av_sync_setting setting;

        pjmedia_av_sync_setting_default(&setting);
        setting.name = (char*) get_fname(filename);
        status = pjmedia_av_sync_create(pool, &setting, &avsync);
        if (status != PJ_SUCCESS)
            goto on_error;

        streams->avsync = avsync;

        for (i = 0; i < nstr; i++) {
            pjmedia_av_sync_media_setting med_setting;

            pjmedia_av_sync_media_setting_default(&med_setting);
            med_setting.type = fport[i]->base.info.fmt.type;
            med_setting.name = (char*)pjmedia_type_name(med_setting.type);
            if (med_setting.type == PJMEDIA_TYPE_AUDIO) {
                med_setting.clock_rate = PJMEDIA_PIA_SRATE(&fport[i]->base.info);
            } else if (med_setting.type == PJMEDIA_TYPE_VIDEO) {
                med_setting.clock_rate = VIDEO_CLOCK_RATE;
            }
            status = pjmedia_av_sync_add_media(avsync, &med_setting,
                                               &fport[i]->avsync_media);
            if (status != PJ_SUCCESS)
                goto on_error;

            /* Set reference timestamps to zeroes */
            status = pjmedia_av_sync_update_ref(fport[i]->avsync_media,
                                                &ts_zero, &ts_zero);
            if (status != PJ_SUCCESS)
                goto on_error;

            /* Set pointer to AVI streams */
            fport[i]->avi_streams = streams;
        }
    }

    status = pj_grp_lock_add_handler(grp_lock, NULL, *p_streams,
                                     &streams_on_destroy);
    if (status != PJ_SUCCESS)
        goto on_error;

    (*p_streams)->pool = pool;

    PJ_LOG(4,(THIS_FILE, 
              "AVI file player '%.*s' created with "
              "%d media ports",
              (int)fport[0]->base.info.name.slen,
              fport[0]->base.info.name.ptr,
              (*p_streams)->num_streams));

    return PJ_SUCCESS;

on_error:
    if (grp_lock) {
        pjmedia_port_destroy(&fport[0]->base);
        for (i = 1; i < nstr; i++)
            pjmedia_port_destroy(&fport[i]->base);
    }
    
    if (streams && streams->avsync) {
        for (i = 0; i < nstr; i++) {
            if (fport[i]->avsync_media)
                pjmedia_av_sync_del_media(NULL, fport[i]->avsync_media);
        }
        pjmedia_av_sync_destroy(streams->avsync);
    }

    pj_pool_release(pool);

    if (status == AVI_EOF)
        return PJMEDIA_EINVALIMEDIATYPE;
    return status;
}

PJ_DEF(unsigned)
pjmedia_avi_streams_get_num_streams(pjmedia_avi_streams *streams)
{
    pj_assert(streams);
    return streams->num_streams;
}

PJ_DEF(unsigned)
pjmedia_avi_streams_get_num_streams_by_media(pjmedia_avi_streams *streams,
                                             pjmedia_type media_type)
{
    unsigned i = 0;
    unsigned num_strm = 0;

    pj_assert(streams);
    for (; i < streams->num_streams; i++)
        if (streams->streams[i]->info.fmt.type == media_type)
            ++num_strm;

    return num_strm;
}

PJ_DEF(pjmedia_avi_stream *)
pjmedia_avi_streams_get_stream(pjmedia_avi_streams *streams,
                               unsigned idx)
{
    pj_assert(streams);
    return (idx < streams->num_streams ? streams->streams[idx] : NULL);
}

PJ_DEF(pjmedia_avi_stream *)
pjmedia_avi_streams_get_stream_by_media(pjmedia_avi_streams *streams,
                                        unsigned start_idx,
                                        pjmedia_type media_type)
{
    unsigned i;

    pj_assert(streams);
    for (i = start_idx; i < streams->num_streams; i++)
        if (streams->streams[i]->info.fmt.type == media_type)
            return streams->streams[i];
    return NULL;
}


/*
 * Get the data length, in bytes.
 */
PJ_DEF(pj_ssize_t) pjmedia_avi_stream_get_len(pjmedia_avi_stream *stream)
{
    struct avi_reader_port *fport;

    /* Sanity check */
    PJ_ASSERT_RETURN(stream, -PJ_EINVAL);

    /* Check that this is really a player port */
    PJ_ASSERT_RETURN(stream->info.signature == SIGNATURE, -PJ_EINVALIDOP);

    fport = (struct avi_reader_port*) stream;

    return (pj_ssize_t)(fport->fsize - fport->start_data);
}


#if !DEPRECATED_FOR_TICKET_2251
/*
 * Register a callback to be called when the file reading has reached the
 * end of file.
 */
PJ_DEF(pj_status_t)
pjmedia_avi_stream_set_eof_cb( pjmedia_avi_stream *stream,
                               void *user_data,
                               pj_status_t (*cb)(pjmedia_avi_stream *stream,
                                                 void *usr_data))
{
    struct avi_reader_port *fport;

    /* Sanity check */
    PJ_ASSERT_RETURN(stream, -PJ_EINVAL);

    /* Check that this is really a player port */
    PJ_ASSERT_RETURN(stream->info.signature == SIGNATURE, -PJ_EINVALIDOP);

    PJ_LOG(1, (THIS_FILE, "pjmedia_avi_stream_set_eof_cb() is deprecated. "
               "Use pjmedia_avi_stream_set_eof_cb2() instead."));

    fport = (struct avi_reader_port*) stream;

    fport->base.port_data.pdata = user_data;
    fport->cb = cb;

    return PJ_SUCCESS;
}
#endif


/*
 * Register a callback to be called when the file reading has reached the
 * end of file.
 */
PJ_DEF(pj_status_t)
pjmedia_avi_stream_set_eof_cb2(pjmedia_avi_stream *stream,
                               void *user_data,
                               void (*cb)(pjmedia_avi_stream *stream,
                                          void *usr_data))
{
    struct avi_reader_port *fport;

    /* Sanity check */
    PJ_ASSERT_RETURN(stream, -PJ_EINVAL);

    /* Check that this is really a player port */
    PJ_ASSERT_RETURN(stream->info.signature == SIGNATURE, -PJ_EINVALIDOP);

    fport = (struct avi_reader_port*) stream;

    fport->base.port_data.pdata = user_data;
    fport->cb2 = cb;

    return PJ_SUCCESS;
}


static pj_status_t file_on_event(pjmedia_event *event,
                                 void *user_data)
{
    struct avi_reader_port *fport = (struct avi_reader_port*)user_data;

    if (event->type == PJMEDIA_EVENT_CALLBACK) {
        if (fport->cb2)
            (*fport->cb2)(&fport->base, fport->base.port_data.pdata);
    }
    
    return PJ_SUCCESS;
}


static pj_status_t skip_forward(pjmedia_port *this_port, pj_size_t frames)
{
    struct avi_reader_port *fport = (struct avi_reader_port*)this_port;
    pj_status_t status = PJ_SUCCESS;
    pj_ssize_t remainder = frames;
    pjmedia_type type = fport->base.info.fmt.type;
    pj_bool_t is_pcm = PJ_FALSE;

    /* For audio, skip current chunk first */
    if (type == PJMEDIA_TYPE_AUDIO) {
        is_pcm = (fport->fmt_id!=PJMEDIA_FORMAT_PCMA &&
                  fport->fmt_id!=PJMEDIA_FORMAT_PCMU);
        if (fport->size_left > 0) {
            pj_ssize_t seek_size = is_pcm? frames * 2 : frames;
            seek_size = PJ_MIN(seek_size, fport->size_left);
            status = pj_file_setpos(fport->fd, seek_size, PJ_SEEK_CUR);
            if (status != PJ_SUCCESS)
                return status;

            fport->size_left -= seek_size;
            remainder -= (seek_size / (is_pcm? 2 : 1));

            fport->frame_cnt += (seek_size / (is_pcm? 2 : 1));
            fport->next_ts.u64 = fport->frame_cnt;
        }
    }

    while (remainder) {
        pjmedia_avi_subchunk ch = {0, 0};
        unsigned stream_id;
        char *cid;

        /* Need to skip new chunk */
        pj_assert(fport->size_left == 0);

        /* Data is padded to the nearest WORD boundary */
        if (fport->pad) {
            status = pj_file_setpos(fport->fd, fport->pad, PJ_SEEK_CUR);
            fport->pad = 0;
        }

        status = file_read(fport->fd, &ch, sizeof(pjmedia_avi_subchunk));
        if (status != PJ_SUCCESS)
            return status;
            
        PJ_CHECK_OVERFLOW_UINT32_TO_LONG(ch.len, return PJ_EINVAL);
        fport->pad = (pj_uint8_t)ch.len & 1;

        cid = (char *)&ch.id;
        if (pj_isdigit(cid[0]) && pj_isdigit(cid[1]))
            stream_id = (cid[0] - '0') * 10 + (cid[1] - '0');
        else
            stream_id = 1000;

        /* We are only interested in data with our stream id */
        if (stream_id != fport->stream_id) {
            if (COMPARE_TAG(ch.id, PJMEDIA_AVI_LIST_TAG))
                PJ_LOG(5, (THIS_FILE, "Unsupported LIST tag found in "
                                        "the movi data."));
            else if (COMPARE_TAG(ch.id, PJMEDIA_AVI_RIFF_TAG)) {
                PJ_LOG(3, (THIS_FILE, "Unsupported format: multiple "
                        "AVIs in a single file."));
                return PJ_ENOTSUP;
            }

            status = pj_file_setpos(fport->fd, ch.len, PJ_SEEK_CUR);
            continue;
        }

        /* Found new chunk */
        fport->size_left = ch.len;
        if (type == PJMEDIA_TYPE_AUDIO) {
            pj_ssize_t seek_size = remainder * (is_pcm? 2 : 1);
            seek_size = PJ_MIN(seek_size, fport->size_left);
            status = pj_file_setpos(fport->fd, seek_size, PJ_SEEK_CUR);
            if (status != PJ_SUCCESS)
                return status;

            fport->size_left -= seek_size;
            remainder -= (seek_size / (is_pcm? 2 : 1));

            fport->frame_cnt += (seek_size / (is_pcm? 2 : 1));
            fport->next_ts.u64 = fport->frame_cnt;
        } else {
            status = pj_file_setpos(fport->fd, fport->size_left, PJ_SEEK_CUR);
            if (status != PJ_SUCCESS)
                return status;
            fport->size_left = 0;
            remainder -= 1;

            fport->frame_cnt++;
            fport->next_ts.u64 = ((pj_uint64_t)fport->frame_cnt *
                                  VIDEO_CLOCK_RATE *
                                  fport->base.info.fmt.det.vid.fps.denum/
                                  fport->base.info.fmt.det.vid.fps.num);
        }
    } /* while (remainder) */

    return PJ_SUCCESS;
}


/*
 * Get frame from file.
 */
static pj_status_t avi_get_frame(pjmedia_port *this_port, 
                                 pjmedia_frame *frame)
{
    struct avi_reader_port *fport = (struct avi_reader_port*)this_port;
    pj_status_t status = PJ_SUCCESS;
    pj_ssize_t size_read = 0, size_to_read = 0;
    pjmedia_port_info* port_info = &fport->base.info;

    pj_assert(fport->base.info.signature == SIGNATURE);

    /* Synchronize media */
    if (fport->avsync_media && !fport->eof) {
        pj_int32_t adjust_delay;
        
        /* Just return if we are increasing delay */
        if (fport->slow_down_frm) {
            fport->slow_down_frm--;
            frame->type = PJMEDIA_FRAME_TYPE_NONE;
            frame->size = 0;
            return PJ_SUCCESS;
        }

        status = pjmedia_av_sync_update_pts(fport->avsync_media,
                                            &fport->next_ts, &adjust_delay);
        if (status == PJ_SUCCESS && adjust_delay) {
            pj_ssize_t frames = 0;

            /* If speed up is requested for more than 1 seconds,
             * the stream may just be resumed, fast forward.
             */
            if (adjust_delay < -1000) {
                PJ_LOG(4,(THIS_FILE, "%.*s: %s need to fast forward by %dms",
                          (int)fport->base.info.name.slen,
                          fport->base.info.name.ptr,
                          pjmedia_type_name(port_info->fmt.type),
                          -adjust_delay));

                if (fport->base.info.fmt.type == PJMEDIA_TYPE_AUDIO) {
                    frames = -adjust_delay *
                             port_info->fmt.det.aud.clock_rate /
                             1000;
                } else {
                    frames = -adjust_delay *
                             port_info->fmt.det.vid.fps.num /
                             port_info->fmt.det.vid.fps.denum / 1000;
                }
                status = skip_forward(this_port, frames);
                if (status != PJ_SUCCESS)
                    goto on_error2;
            }

            /* Otherwise it is a small adjustment, apply for video stream only
             * so the audio playback remains smooth.
             */
            else if (port_info->fmt.type == PJMEDIA_TYPE_VIDEO) {
                pj_bool_t slowdown = adjust_delay > 0;

                adjust_delay = PJ_ABS(adjust_delay);
                frames = adjust_delay *
                         port_info->fmt.det.vid.fps.num /
                         port_info->fmt.det.vid.fps.denum / 1000;

                if (slowdown) {
                    PJ_LOG(4, (THIS_FILE,
                               "%.*s: video need to slow down by %dms",
                               (int)port_info->name.slen,
                               port_info->name.ptr,
                               adjust_delay));
                    frame->type = PJMEDIA_FRAME_TYPE_NONE;
                    frame->size = 0;

                    /* Increase delay */
                    fport->slow_down_frm = frames;
                    return PJ_SUCCESS;
                }
                
                PJ_LOG(4,(THIS_FILE, "%.*s: video need to speed up by %dms",
                                     (int)port_info->name.slen,
                                     port_info->name.ptr,
                                     -adjust_delay));
                status = skip_forward(this_port, (frames? frames : 1));
                if (status != PJ_SUCCESS)
                    goto on_error2;
            }
        }
    }

    /* Set the frame timestamp */
    frame->timestamp.u64 = fport->next_ts.u64;

    /* We encountered end of file */
    if (fport->eof) {
        pj_bool_t no_loop = (fport->options & PJMEDIA_AVI_FILE_NO_LOOP);
        pj_bool_t rewind_now = PJ_TRUE;

        /* If synchronized, wait all streams to EOF before rewinding */
        if (fport->avsync_media) {
            avi_reader_streams *avi_streams = fport->avi_streams;
            
            rewind_now = (avi_streams->eof_cnt %
                          avi_streams->base.num_streams)==0;
            if (rewind_now) {
                pj_timestamp ts_zero = {{0}};
                pjmedia_av_sync_update_ref(fport->avsync_media,
                                           &ts_zero, &ts_zero);
            }
        }

        /* Call callback, if any */
        if (fport->eof != 2) {

            /* To prevent the callback from being called repeatedly */
            fport->eof = 2;

            if (fport->cb2) {
                if (!fport->subscribed) {
                    status = pjmedia_event_subscribe(NULL, &file_on_event,
                                                     fport, fport);
                    fport->subscribed = (status == PJ_SUCCESS)? PJ_TRUE:
                                        PJ_FALSE;
                }

                if (fport->subscribed)  {
                    pjmedia_event event;

                    pjmedia_event_init(&event, PJMEDIA_EVENT_CALLBACK,
                                       NULL, fport);
                    pjmedia_event_publish(NULL, fport, &event,
                                          PJMEDIA_EVENT_PUBLISH_POST_EVENT);
                }
            
                /* Should not access player port after this since
                 * it might have been destroyed by the callback.
                 */
                frame->type = PJMEDIA_FRAME_TYPE_NONE;
                frame->size = 0;
                status = PJ_SUCCESS;

            } else if (fport->cb) {
                status = (*fport->cb)(this_port, fport->base.port_data.pdata);
            }
        }

        /* If callback returns non PJ_SUCCESS or 'no loop' is specified,
         * return immediately (and don't try to access player port since
         * it might have been destroyed by the callback).
         */
        if (status != PJ_SUCCESS || no_loop || !rewind_now) {
            frame->type = PJMEDIA_FRAME_TYPE_NONE;
            frame->size = 0;
            return (no_loop? PJ_EEOF : status);
        }

        if (rewind_now) {
            PJ_LOG(5,(THIS_FILE, "AVI player port %.*s rewinding..",
                      (int)fport->base.info.name.slen,
                      fport->base.info.name.ptr));

            pj_file_setpos(fport->fd, fport->start_data, PJ_SEEK_SET);
            fport->eof = PJ_FALSE;
            fport->frame_cnt = 0;
            fport->next_ts.u64 = 0;
        }
    }

    /* For PCMU/A audio stream, reduce frame size to half (temporarily). */
    if (fport->base.info.fmt.type == PJMEDIA_TYPE_AUDIO &&
        (fport->fmt_id == PJMEDIA_FORMAT_PCMA ||
         fport->fmt_id == PJMEDIA_FORMAT_PCMU))
    {
        frame->size >>= 1;
    }

    /* Fill frame buffer. */
    size_to_read = frame->size;
    do {
        pjmedia_avi_subchunk ch = {0, 0};
        char *cid;
        unsigned stream_id;

        /* We need to read data from the file past the chunk boundary */
        if (fport->size_left > 0 && fport->size_left < size_to_read) {
            status = file_read3(fport->fd, frame->buf, fport->size_left,
                                fport->bits_per_sample, &size_read);
            if (status != PJ_SUCCESS)
                goto on_error2;
            size_to_read -= fport->size_left;
            fport->size_left = 0;
        }

        /* Read new chunk data */
        if (fport->size_left == 0) {
            pj_off_t pos;
            pj_off_t ch_len;

            pj_file_getpos(fport->fd, &pos);

            /* Data is padded to the nearest WORD boundary */
            if (fport->pad) {
                status = pj_file_setpos(fport->fd, fport->pad, PJ_SEEK_CUR);
                fport->pad = 0;
            }

            status = file_read(fport->fd, &ch, sizeof(pjmedia_avi_subchunk));
            if (status != PJ_SUCCESS) {
                size_read = 0;
                goto on_error2;
            }
            
            PJ_CHECK_OVERFLOW_UINT32_TO_LONG(ch.len, 
                                         status = PJ_EINVAL;  goto on_error2;);
            ch_len = ch.len;

            cid = (char *)&ch.id;
            if (cid[0] >= '0' && cid[0] <= '9' &&
                cid[1] >= '0' && cid[1] <= '9') 
            {
                stream_id = (cid[0] - '0') * 10 + (cid[1] - '0');
            } else
                stream_id = 100;
            fport->pad = (pj_uint8_t)ch.len & 1;

            TRACE_((THIS_FILE, "Reading movi data at pos %u (%x), id: %.*s, "
                               "length: %u", (unsigned long)pos,
                               (unsigned long)pos, 4, cid, ch.len));

            /* We are only interested in data with our stream id */
            if (stream_id != fport->stream_id) {
                if (COMPARE_TAG(ch.id, PJMEDIA_AVI_LIST_TAG))
                    PJ_LOG(5, (THIS_FILE, "Unsupported LIST tag found in "
                                          "the movi data."));
                else if (COMPARE_TAG(ch.id, PJMEDIA_AVI_RIFF_TAG)) {
                    PJ_LOG(3, (THIS_FILE, "Unsupported format: multiple "
                           "AVIs in a single file."));
                    status = AVI_EOF;
                    goto on_error2;
                }

                status = pj_file_setpos(fport->fd, ch_len, PJ_SEEK_CUR);
                continue;
            }
            fport->size_left = ch.len;
        }

        frame->type = (fport->base.info.fmt.type == PJMEDIA_TYPE_VIDEO ?
                       PJMEDIA_FRAME_TYPE_VIDEO : PJMEDIA_FRAME_TYPE_AUDIO);

        if (frame->type == PJMEDIA_FRAME_TYPE_AUDIO) {
            if (size_to_read > fport->size_left)
                size_to_read = fport->size_left;
            status = file_read3(fport->fd, (char *)frame->buf + frame->size -
                                size_to_read, size_to_read,
                                fport->bits_per_sample, &size_read);
            if (status != PJ_SUCCESS)
                goto on_error2;
            fport->size_left -= size_to_read;
        } else {
            pj_assert(frame->size >= ch.len);
            status = file_read3(fport->fd, frame->buf, ch.len,
                                0, &size_read);
            if (status != PJ_SUCCESS)
                goto on_error2;
            frame->size = ch.len;
            fport->size_left = 0;
        }

        break;

    } while(1);

    if (frame->type == PJMEDIA_FRAME_TYPE_AUDIO) {

        /* Decode PCMU/A frame */
        if (fport->fmt_id == PJMEDIA_FORMAT_PCMA ||
            fport->fmt_id == PJMEDIA_FORMAT_PCMU)
        {
            unsigned i;
            pj_uint16_t *dst;
            pj_uint8_t *src;

            dst = (pj_uint16_t*)frame->buf + frame->size - 1;
            src = (pj_uint8_t*)frame->buf + frame->size - 1;

            if (fport->fmt_id == PJMEDIA_FORMAT_PCMU) {
                for (i = 0; i < frame->size; ++i) {
                    *dst-- = (pj_uint16_t) pjmedia_ulaw2linear(*src--);
                }
            } else {
                for (i = 0; i < frame->size; ++i) {
                    *dst-- = (pj_uint16_t) pjmedia_alaw2linear(*src--);
                }
            }

            /* Return back the frame size */
            frame->size <<= 1;
        }

        fport->frame_cnt += (frame->size >> 1);
        fport->next_ts.u64 = fport->frame_cnt;
    } else {
        fport->frame_cnt++;
        fport->next_ts.u64 = ((pj_uint64_t)fport->frame_cnt *
                              VIDEO_CLOCK_RATE *
                              fport->base.info.fmt.det.vid.fps.denum/
                              fport->base.info.fmt.det.vid.fps.num);
    }

    return PJ_SUCCESS;

on_error2:
    if (status == AVI_EOF && !fport->eof) {

        /* Reset AV sync on the last stream encountering EOF */
        if (fport->avsync_media) {
            avi_reader_streams *avi_streams = fport->avi_streams;

            if (avi_streams->avsync &&
                (++avi_streams->eof_cnt %
                 avi_streams->base.num_streams == 0))
            {
                pjmedia_av_sync_reset(avi_streams->avsync);
            }
        }

        fport->eof = PJ_TRUE;

        PJ_LOG(5,(THIS_FILE, "AVI player port %.*s EOF",
                  (int)fport->base.info.name.slen,
                  fport->base.info.name.ptr));

        size_to_read -= size_read;
        if (size_to_read == (pj_ssize_t)frame->size) {
            /* Frame is empty */
            frame->type = PJMEDIA_FRAME_TYPE_NONE;
            frame->size = 0;
            return PJ_EEOF;
        }
        pj_bzero((char *)frame->buf + frame->size - size_to_read,
                 size_to_read);

        return PJ_SUCCESS;
    }

    frame->type = PJMEDIA_FRAME_TYPE_NONE;
    frame->size = 0;
    return (status==AVI_EOF? PJ_EEOF : status);
}

/*
 * Destroy port.
 */
static pj_status_t avi_on_destroy(pjmedia_port *this_port)
{
    struct avi_reader_port *fport = (struct avi_reader_port*) this_port;

    pj_assert(this_port->info.signature == SIGNATURE);

    if (fport->subscribed) {
        pjmedia_event_unsubscribe(NULL, &file_on_event, fport, fport);
        fport->subscribed = PJ_FALSE;
    }

    if (fport->fd != (pj_oshandle_t) (pj_ssize_t)-1)
        pj_file_close(fport->fd);

    if (fport->avsync_media) {
        pjmedia_av_sync_del_media(NULL, fport->avsync_media);
        fport->avsync_media = NULL;
    }

    return PJ_SUCCESS;
}


#endif /* PJMEDIA_HAS_VIDEO */
