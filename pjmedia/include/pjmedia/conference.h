/* 
 * Copyright (C) 2008-2011 Teluu Inc. (http://www.teluu.com)
 * Copyright (C) 2003-2008 Benny Prijono <benny@prijono.org>
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
#ifndef __PJMEDIA_CONF_H__
#define __PJMEDIA_CONF_H__


/**
 * @file conference.h
 * @brief Conference bridge.
 */
#include <pjmedia/port.h>

/**
 * @defgroup PJMEDIA_CONF Conference Bridge
 * @ingroup PJMEDIA_PORT
 * @brief Audio conference bridge implementation
 * @{
 *
 * This describes the conference bridge implementation in PJMEDIA. The
 * conference bridge provides powerful and very efficient mechanism to
 * route the audio flow and mix the audio signal when required.
 *
 * Some more information about the media flow when conference bridge is
 * used is described in
 * https://docs.pjsip.org/en/latest/specific-guides/media/audio_flow.html .
 */

PJ_BEGIN_DECL

/* Since 1.3 pjmedia_conf_add_passive_port() has been deprecated
 * and replaced by splitcomb.
 * See also https://github.com/pjsip/pjproject/issues/2234.
 */
#ifndef DEPRECATED_FOR_TICKET_2234
#  define DEPRECATED_FOR_TICKET_2234    1
#endif

/**
 * The conference bridge signature in pjmedia_port_info.
 */
#define PJMEDIA_CONF_BRIDGE_SIGNATURE   PJMEDIA_SIG_PORT_CONF

/**
 * The audio switchboard signature in pjmedia_port_info.
 */
#define PJMEDIA_CONF_SWITCH_SIGNATURE   PJMEDIA_SIG_PORT_CONF_SWITCH


/**
 * Opaque type for conference bridge.
 */
typedef struct pjmedia_conf pjmedia_conf;

/**
 * Conference port info.
 */
typedef struct pjmedia_conf_port_info
{
    unsigned            slot;               /**< Slot number.               */
    pj_str_t            name;               /**< Port name.                 */
    pjmedia_format      format;             /**< Format.                    */
    pjmedia_port_op     tx_setting;         /**< Transmit settings.         */
    pjmedia_port_op     rx_setting;         /**< Receive settings.          */
    unsigned            listener_cnt;       /**< Number of listeners.       */
    unsigned           *listener_slots;     /**< Array of listeners.        */
    unsigned           *listener_adj_level; /**< Array of listeners' level
                                                 adjustment                 */
    unsigned            transmitter_cnt;    /**< Number of transmitter.     */
    unsigned            clock_rate;         /**< Clock rate of the port.    */
    unsigned            channel_count;      /**< Number of channels.        */
    unsigned            samples_per_frame;  /**< Samples per frame          */
    unsigned            bits_per_sample;    /**< Bits per sample.           */
    int                 tx_adj_level;       /**< Tx level adjustment.       */
    int                 rx_adj_level;       /**< Rx level adjustment.       */
} pjmedia_conf_port_info;

/** 
 * Conference operation type enumeration.
 */
typedef enum pjmedia_conf_op_type
{
    /**
     * The operation is unknown.
     */
    PJMEDIA_CONF_OP_UNKNOWN,

    /**
     * The adding port operation.
     */
    PJMEDIA_CONF_OP_ADD_PORT,

    /**
     * The remove port operation.
     */
    PJMEDIA_CONF_OP_REMOVE_PORT,

    /**
     * The connect ports (start transmit) operation.
     */
    PJMEDIA_CONF_OP_CONNECT_PORTS,

    /**
     * The disconnect ports (stop transmit) operation.
     */
    PJMEDIA_CONF_OP_DISCONNECT_PORTS

} pjmedia_conf_op_type;

/**
 * Conference operation parameter.
 */
typedef union pjmedia_conf_op_param
{
    /**
     * The information for adding port operation.
     */
    struct {
        unsigned port;      /**< The port id.                           */
    } add_port;

    /**
     * The information for removing port operation.
     */
    struct {
        unsigned port;      /**< The port id.                           */
    } remove_port;

    /**
     * The information for connecting port operation.
     */
    struct {
        unsigned src;       /**< The source port id.                    */
        unsigned sink;      /**< The destination port id.               */
        int adj_level;      /**< The adjustment level.                  */
    } connect_ports;

    /**
     * The information for disconnecting port operation.
     */
    struct {
        unsigned src;       /**< The source port id. For multiple port
                                 operation, this will be set to -1.     */
        unsigned sink;      /**< The destination port id. For multiple
                                 port operation, this will be set
                                 to -1.                                 */
    } disconnect_ports;

} pjmedia_conf_op_param;

/**
 * This will contain the information of the conference operation.
 */
typedef struct pjmedia_conf_op_info
{
    /**
     * The operation type.
     */
    pjmedia_conf_op_type    op_type;

    /**
     * The operation return status.
     */
    pj_status_t             status;

    /**
     * The operation data.
     */
    pjmedia_conf_op_param   op_param;

} pjmedia_conf_op_info;

/**
  * The callback type to be called upon the completion of a conference 
  * port operation.
  *
  * @param info      The conference op callback param.
  *
  */
typedef void (*pjmedia_conf_op_cb)(const pjmedia_conf_op_info *info);

/**
 * Conference port options. The values here can be combined in bitmask to
 * be specified when the conference bridge is created.
 */
enum pjmedia_conf_option
{
    PJMEDIA_CONF_NO_MIC  = 1,   /**< Disable audio streams from the
                                     microphone device.                     */
    PJMEDIA_CONF_NO_DEVICE = 2, /**< Do not create sound device.            */
    PJMEDIA_CONF_SMALL_FILTER=4,/**< Use small filter table when resampling */
    PJMEDIA_CONF_USE_LINEAR=8   /**< Use linear resampling instead of filter
                                     based.                                 */
};

/**
 * This structure specifies the conference bridge creation parameters.
 */
typedef struct pjmedia_conf_param
{
    /**
     * Maximum number of slots/ports to be created in
     * the bridge. Note that the bridge internally uses
     * one port for the sound device, so the actual 
     * maximum number of ports will be less one than
     * this value.
     */
    unsigned max_slots;

    /**
     * Set the sampling rate of the bridge. This value
     * is also used to set the sampling rate of the
     * sound device.
     */
    unsigned sampling_rate;

    /**
     * Number of channels in the PCM stream. Normally
     * the value will be 1 for mono, but application may
     * specify a value of 2 for stereo. Note that all
     * ports that will be connected to the bridge MUST 
     * have the same number of channels as the bridge.
     */
    unsigned channel_count;

    /**
     * Set the number of samples per frame. This value
     * is also used to set the sound device.
     */
    unsigned samples_per_frame;

    /**
     * Set the number of bits per sample. This value
     * is also used to set the sound device. Currently
     * only 16bit per sample is supported.
     */
    unsigned bits_per_sample;

    /**
     * Bitmask options to be set for the bridge. The
     * options are constructed from #pjmedia_conf_option
     * enumeration.
     * The default value is zero.
     */
    unsigned options;

    /** 
     * The number of worker threads to use by conference bridge.
     * Zero means the operations will be done only by get_frame() thread, 
     * i.e. conference bridge will be sequential.
     * Set this parameter to non-zero value to enable parallel processing.
     * The number of worker threads should be less than or equal to the number 
     * of the processor cores. However, the optimal number of worker threads
     * is application and hardware dependent.
     * The default value is zero - sequential conference bridge.
     * This value is compatible with previous behavior.
     * At compile time application developer can change the default value by 
     * setting #PJMEDIA_CONF_THREADS macro in the config_site.h.
     * PJMEDIA_CONF_THREADS is total number of conference bridge threads 
     * including get_frame() thread. worker_threads is the number of conference
     * bridge threads excluding get_frame() thread. 
     * As a general rule worker_threads is 1 less than PJMEDIA_CONF_THREADS.
     * This value is ignored by all conference backends except for the 
     * multithreaded conference bridge backend
     * (PJMEDIA_CONF_PARALLEL_BRIDGE_BACKEND).
     * 
     * The total number of conference bridge threads can be configured at the
     * pjsua level using the pjsua_media_config::conf_threads parameter, or at
     * the pjsua2 level using the pjsua2::MediaConfig::confThreads parameter.
     */
    unsigned worker_threads;
} pjmedia_conf_param;


/**
 * Initialize conference bridge creation parameters.
 */
PJ_INLINE(void) pjmedia_conf_param_default(pjmedia_conf_param *param)
{
    pj_bzero(param, sizeof(pjmedia_conf_param));
    /* Set the default values */
#if defined(PJMEDIA_CONF_THREADS) && PJMEDIA_CONF_THREADS > 1
    param->worker_threads = PJMEDIA_CONF_THREADS-1;
#endif
}

/**
 * Create conference bridge with the specified parameters. The sampling rate,
 * samples per frame, and bits per sample will be used for the internal
 * operation of the bridge (e.g. when mixing audio frames). However, ports 
 * with different configuration may be connected to the bridge. In this case,
 * the bridge is able to perform sampling rate conversion, and buffering in 
 * case the samples per frame is different.
 *
 * For this version of PJMEDIA, only 16bits per sample is supported.
 *
 * For this version of PJMEDIA, the channel count of the ports MUST match
 * the channel count of the bridge.
 *
 * Under normal operation (i.e. when PJMEDIA_CONF_NO_DEVICE option is NOT
 * specified), the bridge internally create an instance of sound device
 * and connect the sound device to port zero of the bridge. 
 *
 * If PJMEDIA_CONF_NO_DEVICE options is specified, no sound device will
 * be created in the conference bridge. Application MUST acquire the port
 * interface of the bridge by calling #pjmedia_conf_get_master_port(), and
 * connect this port interface to a sound device port by calling
 * #pjmedia_snd_port_connect(), or to a master port (pjmedia_master_port)
 * if application doesn't want to instantiate any sound devices.
 *
 * The sound device or master port are crucial for the bridge's operation, 
 * because it provides the bridge with necessary clock to process the audio
 * frames periodically. Internally, the bridge runs when get_frame() to 
 * port zero is called.
 *
 * @param pool              Pool to use to allocate the bridge and 
 *                          additional buffers for the sound device.
 * @param max_slots         Maximum number of slots/ports to be created in
 *                          the bridge. Note that the bridge internally uses
 *                          one port for the sound device, so the actual 
 *                          maximum number of ports will be less one than
 *                          this value.
 * @param sampling_rate     Set the sampling rate of the bridge. This value
 *                          is also used to set the sampling rate of the
 *                          sound device.
 * @param channel_count     Number of channels in the PCM stream. Normally
 *                          the value will be 1 for mono, but application may
 *                          specify a value of 2 for stereo. Note that all
 *                          ports that will be connected to the bridge MUST 
 *                          have the same number of channels as the bridge.
 * @param samples_per_frame Set the number of samples per frame. This value
 *                          is also used to set the sound device.
 * @param bits_per_sample   Set the number of bits per sample. This value
 *                          is also used to set the sound device. Currently
 *                          only 16bit per sample is supported.
 * @param options           Bitmask options to be set for the bridge. The
 *                          options are constructed from #pjmedia_conf_option
 *                          enumeration.
 * @param p_conf            Pointer to receive the conference bridge instance.
 *
 * @return                  PJ_SUCCESS if conference bridge can be created.
 */
PJ_DECL(pj_status_t) pjmedia_conf_create( pj_pool_t *pool,
                                          unsigned max_slots,
                                          unsigned sampling_rate,
                                          unsigned channel_count,
                                          unsigned samples_per_frame,
                                          unsigned bits_per_sample,
                                          unsigned options,
                                          pjmedia_conf **p_conf );

/**
 * Create conference bridge with the specified parameters. The sampling rate,
 * samples per frame, and bits per sample will be used for the internal
 * operation of the bridge (e.g. when mixing audio frames). However, ports
 * with different configuration may be connected to the bridge. In this case,
 * the bridge is able to perform sampling rate conversion, and buffering in
 * case the samples per frame is different.
 *
 * For this version of PJMEDIA, only 16bits per sample is supported.
 *
 * For this version of PJMEDIA, the channel count of the ports MUST match
 * the channel count of the bridge.
 *
 * Under normal operation (i.e. when PJMEDIA_CONF_NO_DEVICE option is NOT
 * specified), the bridge internally create an instance of sound device
 * and connect the sound device to port zero of the bridge.
 *
 * If PJMEDIA_CONF_NO_DEVICE options is specified, no sound device will
 * be created in the conference bridge. Application MUST acquire the port
 * interface of the bridge by calling #pjmedia_conf_get_master_port(), and
 * connect this port interface to a sound device port by calling
 * #pjmedia_snd_port_connect(), or to a master port (pjmedia_master_port)
 * if application doesn't want to instantiate any sound devices.
 *
 * The sound device or master port are crucial for the bridge's operation,
 * because it provides the bridge with necessary clock to process the audio
 * frames periodically. Internally, the bridge runs when get_frame() to
 * port zero is called.
 *
 * @param pool              Pool to use to allocate the bridge and
 *                          additional buffers for the sound device.
 * @param param             The conference bridge creation parameters.
 *                          See #pjmedia_conf_param for more information.
 * @param p_conf            Pointer to receive the conference bridge instance.
 *
 * @return                  PJ_SUCCESS if conference bridge can be created.
 */
PJ_DECL(pj_status_t) pjmedia_conf_create2(pj_pool_t *pool,
                                          pjmedia_conf_param *param,
                                          pjmedia_conf **p_conf);


/**
 * Destroy conference bridge. This will also remove any port, thus application
 * might get notified from the callback set from #pjmedia_conf_set_op_cb(). 
 *
 * @param conf              The conference bridge.
 *
 * @return                  PJ_SUCCESS on success.
 */
PJ_DECL(pj_status_t) pjmedia_conf_destroy( pjmedia_conf *conf );

/**
 * Register the callback to be called when a port operation has been
 * completed.
 * 
 * The callback will most likely be called from media threads,
 * thus application must not perform long/blocking processing in this callback.
 * 
 * @param conf          The conference bridge.
 * @param cb            Callback to be called. Set this to NULL to unregister
 *                      the callback.
 *
 * @return              PJ_SUCCESS on success.
 */
PJ_DECL(pj_status_t) pjmedia_conf_set_op_cb(pjmedia_conf *conf,
                                            pjmedia_conf_op_cb cb);

/**
 * Get the master port interface of the conference bridge. The master port
 * corresponds to the port zero of the bridge. This is only usefull when 
 * application wants to manage the sound device by itself, instead of 
 * allowing the bridge to automatically create a sound device implicitly.
 *
 * This function will only return a port interface if PJMEDIA_CONF_NO_DEVICE
 * option was specified when the bridge was created.
 *
 * Application can connect the port returned by this function to a 
 * sound device by calling #pjmedia_snd_port_connect().
 *
 * @param conf              The conference bridge.
 *
 * @return                  The port interface of port zero of the bridge,
 *                          only when PJMEDIA_CONF_NO_DEVICE options was
 *                          specified when the bridge was created.
 */
PJ_DECL(pjmedia_port*) pjmedia_conf_get_master_port(pjmedia_conf *conf);


/**
 * Set master port name.
 *
 * @param conf              The conference bridge.
 * @param name              Name to be assigned.
 *
 * @return                  PJ_SUCCESS on success.
 */
PJ_DECL(pj_status_t) pjmedia_conf_set_port0_name(pjmedia_conf *conf,
                                                 const pj_str_t *name);


/**
 * Add media port to the conference bridge.
 *
 * By default, the new conference port will have both TX and RX enabled, 
 * but it is not connected to any other ports. Application SHOULD call 
 * #pjmedia_conf_connect_port() to  enable audio transmission and receipt 
 * to/from this port.
 *
 * Once the media port is connected to other port(s) in the bridge,
 * the bridge will continuosly call get_frame() and put_frame() to the
 * port, allowing media to flow to/from the port.
 * 
 * This operation executes asynchronously, use the callback set from
 * #pjmedia_conf_set_op_cb() to receive notification upon completion.
 * 
 * Note: Sample rate and ptime (frame duration) settings must be compatible.
 * Configurations resulting in a fractional number of samples per frame
 * are not supported and will cause the function to fail.
 * For example, a sample rate of 22050 Hz and a frame duration (ptime) of 10 ms
 * will result in 220.5 samples per frame, which is not an integer, 
 * so port creation will fail.
 *
 * @param conf          The conference bridge.
 * @param pool          Pool to allocate buffers for this port.
 * @param strm_port     Stream port interface.
 * @param name          Optional name for the port. If this value is NULL,
 *                      the name will be taken from the name in the port 
 *                      info.
 * @param p_slot        Pointer to receive the slot index of the port in
 *                      the conference bridge.
 *
 * @return              PJ_SUCCESS on success.
 */
PJ_DECL(pj_status_t) pjmedia_conf_add_port( pjmedia_conf *conf,
                                            pj_pool_t *pool,
                                            pjmedia_port *strm_port,
                                            const pj_str_t *name,
                                            unsigned *p_slot );


#if !DEPRECATED_FOR_TICKET_2234
/**
 * <i><b>Warning:</b> This API has been deprecated since 1.3 and will be
 * removed in the future release, use @ref PJMEDIA_SPLITCOMB instead.</i>
 *
 * Create and add a passive media port to the conference bridge. Unlike
 * "normal" media port that is added with #pjmedia_conf_add_port(), media
 * port created with this function will not have its get_frame() and
 * put_frame() called by the bridge; instead, application MUST continuosly
 * call these functions to the port, to allow media to flow from/to the
 * port.
 *
 * Upon return of this function, application will be given two objects:
 * the slot number of the port in the bridge, and pointer to the media
 * port where application MUST start calling get_frame() and put_frame()
 * to the port.
 *
 * @param conf              The conference bridge.
 * @param pool              Pool to allocate buffers etc for this port.
 * @param name              Name to be assigned to the port.
 * @param clock_rate        Clock rate/sampling rate.
 * @param channel_count     Number of channels.
 * @param samples_per_frame Number of samples per frame.
 * @param bits_per_sample   Number of bits per sample.
 * @param options           Options (should be zero at the moment).
 * @param p_slot            Pointer to receive the slot index of the port in
 *                          the conference bridge.
 * @param p_port            Pointer to receive the port instance.
 *
 * @return                  PJ_SUCCESS on success, or the appropriate error 
 *                          code.
 */
PJ_DECL(pj_status_t) pjmedia_conf_add_passive_port( pjmedia_conf *conf,
                                                    pj_pool_t *pool,
                                                    const pj_str_t *name,
                                                    unsigned clock_rate,
                                                    unsigned channel_count,
                                                    unsigned samples_per_frame,
                                                    unsigned bits_per_sample,
                                                    unsigned options,
                                                    unsigned *p_slot,
                                                    pjmedia_port **p_port );

#endif

/**
 * Change TX and RX settings for the port.
 *
 * @param conf          The conference bridge.
 * @param slot          Port number/slot in the conference bridge.
 * @param tx            Settings for the transmission TO this port.
 * @param rx            Settings for the receipt FROM this port.
 *
 * @return              PJ_SUCCESS on success.
 */
PJ_DECL(pj_status_t) pjmedia_conf_configure_port( pjmedia_conf *conf,
                                                  unsigned slot,
                                                  pjmedia_port_op tx,
                                                  pjmedia_port_op rx);


/**
 * Enable unidirectional audio from the specified source slot to the specified
 * sink slot.
 * Application may adjust the level to make signal transmitted from the source
 * slot to the sink slot either louder or more quiet. The level adjustment is
 * calculated with this formula:
 * <b><tt>output = input * (adj_level+128) / 128</tt></b>. Using this, zero
 * indicates no adjustment, the value -128 will mute the signal, and the value
 * of +128 will make the signal 100% louder, +256 will make it 200% louder,
 * etc.
 *
 * The level adjustment will apply to a specific connection only (i.e. only
 * for the signal from the source to the sink), as compared to
 * pjmedia_conf_adjust_tx_level()/pjmedia_conf_adjust_rx_level() which
 * applies to all signals from/to that port. The signal adjustment
 * will be cumulative, in this following order:
 * signal from the source will be adjusted with the level specified
 * in pjmedia_conf_adjust_rx_level(), then with the level specified
 * via this API, and finally with the level specified to the sink's
 * pjmedia_conf_adjust_tx_level().
 * 
 * This operation executes asynchronously, use the callback set from
 * #pjmedia_conf_set_op_cb() to receive notification upon completion.
 *
 * @param conf          The conference bridge.
 * @param src_slot      Source slot.
 * @param sink_slot     Sink slot.
 * @param adj_level     Adjustment level, which must be greater than or equal
 *                      to -128. A value of zero means there is no level
 *                      adjustment to be made, the value -128 will mute the
 *                      signal, and the value of +128 will make the signal
 *                      100% louder, +256 will make it 200% louder, etc.
 *                      See the function description for the formula.
 *
 * @return              PJ_SUCCES on success.
 */
PJ_DECL(pj_status_t) pjmedia_conf_connect_port( pjmedia_conf *conf,
                                                unsigned src_slot,
                                                unsigned sink_slot,
                                                int adj_level );


/**
 * Disconnect unidirectional audio from the specified source to the specified
 * sink slot.
 *
 * Note that the operation will be done asynchronously, so application
 * should not assume that the port will no longer receive/send audio frame
 * after this function has returned. Use the callback set from
 * #pjmedia_conf_set_op_cb() to receive notification upon completion.
 *
 * @param conf          The conference bridge.
 * @param src_slot      Source slot.
 * @param sink_slot     Sink slot.
 *
 * @return              PJ_SUCCESS on success.
 */
PJ_DECL(pj_status_t) pjmedia_conf_disconnect_port( pjmedia_conf *conf,
                                                   unsigned src_slot,
                                                   unsigned sink_slot );


/**
 * Disconnect unidirectional audio from all sources to the specified sink slot.
 *
 * Note that the operation will be done asynchronously, so application
 * should not assume that the port will no longer receive/send audio frame
 * after this function has returned. Use the callback set from
 * #pjmedia_conf_set_op_cb() to receive notification upon completion.
 *
 * @param conf          The conference bridge.
 * @param sink_slot     Sink slot.
 *
 * @return              PJ_SUCCESS on success.
 */
PJ_DECL(pj_status_t)
pjmedia_conf_disconnect_port_from_sources( pjmedia_conf *conf,
                                           unsigned sink_slot);


/**
 * Disconnect unidirectional audio from the specified source to all sink slots.
 *
 * Note that the operation will be done asynchronously, so application
 * should not assume that the port will no longer receive/send audio frame
 * after this function has returned. Use the callback set from
 * #pjmedia_conf_set_op_cb() to receive notification upon completion.
 *
 * @param conf          The conference bridge.
 * @param src_slot      Source slot.
 *
 * @return              PJ_SUCCESS on success.
 */
PJ_DECL(pj_status_t)
pjmedia_conf_disconnect_port_from_sinks( pjmedia_conf *conf,
                                         unsigned src_slot);


/**
 * Get number of ports currently registered to the conference bridge.
 *
 * @param conf          The conference bridge.
 *
 * @return              Number of ports currently registered to the conference
 *                      bridge.
 */
PJ_DECL(unsigned) pjmedia_conf_get_port_count(pjmedia_conf *conf);


/**
 * Get total number of ports connections currently set up in the bridge.
 * 
 * @param conf          The conference bridge.
 *
 * @return              PJ_SUCCESS on success.
 */
PJ_DECL(unsigned) pjmedia_conf_get_connect_count(pjmedia_conf *conf);


/**
 * Remove the specified port from the conference bridge.
 *
 * Note that the operation will be done asynchronously, so application
 * should not assume that the port will no longer receive/send audio frame
 * after this function has returned.
 *
 * If the port uses any app's resources, application should maintain
 * the resources validity until the port is completely removed. Application
 * can schedule the resource release via #pjmedia_conf_add_destroy_handler().
 * 
 * This operation executes asynchronously, use the callback set from
 * #pjmedia_conf_set_op_cb() to receive notification upon completion.
 *
 * @param conf          The conference bridge.
 * @param slot          The port index to be removed.
 *
 * @return              PJ_SUCCESS on success.
 */
PJ_DECL(pj_status_t) pjmedia_conf_remove_port( pjmedia_conf *conf,
                                               unsigned slot );



/**
 * Enumerate occupied ports in the bridge.
 *
 * @param conf          The conference bridge.
 * @param ports         Array of port numbers to be filled in.
 * @param count         On input, specifies the maximum number of ports
 *                      in the array. On return, it will be filled with
 *                      the actual number of ports.
 *
 * @return              PJ_SUCCESS on success.
 */
PJ_DECL(pj_status_t) pjmedia_conf_enum_ports( pjmedia_conf *conf,
                                              unsigned ports[],
                                              unsigned *count );


/**
 * Get port info.
 *
 * @param conf          The conference bridge.
 * @param slot          Port index.
 * @param info          Pointer to receive the info.
 *
 * @return              PJ_SUCCESS on success.
 */
PJ_DECL(pj_status_t) pjmedia_conf_get_port_info( pjmedia_conf *conf,
                                                 unsigned slot,
                                                 pjmedia_conf_port_info *info);


/**
 * Get occupied ports info.
 *
 * @param conf          The conference bridge.
 * @param size          On input, contains maximum number of infos
 *                      to be retrieved. On output, contains the actual
 *                      number of infos that have been copied.
 * @param info          Array of info.
 *
 * @return              PJ_SUCCESS on success.
 */
PJ_DECL(pj_status_t) pjmedia_conf_get_ports_info(pjmedia_conf *conf,
                                                 unsigned *size,
                                                 pjmedia_conf_port_info info[]
                                                 );


/**
 * Get last signal level transmitted to or received from the specified port.
 * This will retrieve the "real-time" signal level of the audio as they are
 * transmitted or received by the specified port. Application may call this
 * function periodically to display the signal level to a VU meter.
 *
 * The signal level is an integer value in zero to 255, with zero indicates
 * no signal, and 255 indicates the loudest signal level.
 *
 * @param conf          The conference bridge.
 * @param slot          Slot number.
 * @param tx_level      Optional argument to receive the level of signal
 *                      transmitted to the specified port (i.e. the direction
 *                      is from the bridge to the port).
 * @param rx_level      Optional argument to receive the level of signal
 *                      received from the port (i.e. the direction is from the
 *                      port to the bridge).
 *
 * @return              PJ_SUCCESS on success.
 */
PJ_DECL(pj_status_t) pjmedia_conf_get_signal_level(pjmedia_conf *conf,
                                                   unsigned slot,
                                                   unsigned *tx_level,
                                                   unsigned *rx_level);


/**
 * Adjust the level of signal received from the specified port.
 * Application may adjust the level to make signal received from the port
 * either louder or more quiet. The level adjustment is calculated with this
 * formula: <b><tt>output = input * (adj_level+128) / 128</tt></b>. Using 
 * this, zero indicates no adjustment, the value -128 will mute the signal, 
 * and the value of +128 will make the signal 100% louder, +256 will make it
 * 200% louder, etc.
 *
 * The level adjustment value will stay with the port until the port is
 * removed from the bridge or new adjustment value is set. The current
 * level adjustment value is reported in the media port info when
 * the #pjmedia_conf_get_port_info() function is called.
 *
 * @param conf          The conference bridge.
 * @param slot          Slot number of the port.
 * @param adj_level     Adjustment level, which must be greater than or equal
 *                      to -128. A value of zero means there is no level
 *                      adjustment to be made, the value -128 will mute the 
 *                      signal, and the value of +128 will make the signal 
 *                      100% louder, +256 will make it 200% louder, etc. 
 *                      See the function description for the formula.
 *
 * @return              PJ_SUCCESS on success.
 */
PJ_DECL(pj_status_t) pjmedia_conf_adjust_rx_level( pjmedia_conf *conf,
                                                   unsigned slot,
                                                   int adj_level );


/**
 * Adjust the level of signal to be transmitted to the specified port.
 * Application may adjust the level to make signal transmitted to the port
 * either louder or more quiet. The level adjustment is calculated with this
 * formula: <b><tt>output = input * (adj_level+128) / 128</tt></b>. Using 
 * this, zero indicates no adjustment, the value -128 will mute the signal, 
 * and the value of +128 will make the signal 100% louder, +256 will make it
 * 200% louder, etc.
 *
 * The level adjustment value will stay with the port until the port is
 * removed from the bridge or new adjustment value is set. The current
 * level adjustment value is reported in the media port info when
 * the #pjmedia_conf_get_port_info() function is called.
 *
 * @param conf          The conference bridge.
 * @param slot          Slot number of the port.
 * @param adj_level     Adjustment level, which must be greater than or equal
 *                      to -128. A value of zero means there is no level
 *                      adjustment to be made, the value -128 will mute the 
 *                      signal, and the value of +128 will make the signal 
 *                      100% louder, +256 will make it 200% louder, etc. 
 *                      See the function description for the formula.
 *
 * @return              PJ_SUCCESS on success.
 */
PJ_DECL(pj_status_t) pjmedia_conf_adjust_tx_level( pjmedia_conf *conf,
                                                   unsigned slot,
                                                   int adj_level );


/**
 * Adjust the level of signal to be transmitted from the source slot to the
 * sink slot.
 * Application may adjust the level to make signal transmitted from the source
 * slot to the sink slot either louder or more quiet. The level adjustment is
 * calculated with this formula:
 * <b><tt>output = input * (adj_level+128) / 128</tt></b>. Using this, zero
 * indicates no adjustment, the value -128 will mute the signal, and the value
 * of +128 will make the signal 100% louder, +256 will make it 200% louder,
 * etc.
 *
 * The level adjustment value will stay with the connection until the
 * connection is removed or new adjustment value is set. The current level
 * adjustment value is reported in the media port info when the
 * #pjmedia_conf_get_port_info() function is called.
 *
 * @param conf          The conference bridge.
 * @param src_slot      Source slot.
 * @param sink_slot     Sink slot.
 * @param adj_level     Adjustment level, which must be greater than or equal
 *                      to -128. A value of zero means there is no level
 *                      adjustment to be made, the value -128 will mute the 
 *                      signal, and the value of +128 will make the signal 
 *                      100% louder, +256 will make it 200% louder, etc. 
 *                      See the function description for the formula.
 *
 * @return              PJ_SUCCESS on success.
 */
PJ_DECL(pj_status_t) pjmedia_conf_adjust_conn_level( pjmedia_conf *conf,
                                                     unsigned src_slot,
                                                     unsigned sink_slot,
                                                     int adj_level );


/**
 * Add port destructor handler.
 *
 * Application can use this function to schedule resource release.
 * Note that application cannot release any app's resources used by the port,
 * e.g: memory pool, database connection, immediately after removing the port
 * from the conference bridge as port removal is asynchronous.
 *
 * Usually this function is called after adding the port to the conference
 * bridge.
 *
 * @param conf              The conference bridge.
 * @param slot              The port slot index.
 * @param member            A pointer to be passed to the handler.
 * @param handler           The destroy handler.
 *
 * @return                  PJ_SUCCESS on success.
 */
PJ_DECL(pj_status_t) pjmedia_conf_add_destroy_handler(
                                            pjmedia_conf* conf,
                                            unsigned slot,
                                            void* member,
                                            pj_grp_lock_handler handler);


/**
 * Remove previously registered destructor handler.
 *
 * @param conf              The conference bridge.
 * @param slot              The port slot index.
 * @param member            A pointer to be passed to the handler.
 * @param handler           The destroy handler.
 *
 * @return                  PJ_SUCCESS on success.
 */
PJ_DECL(pj_status_t) pjmedia_conf_del_destroy_handler(
                                            pjmedia_conf* conf,
                                            unsigned slot,
                                            void* member,
                                            pj_grp_lock_handler handler);


PJ_END_DECL


/**
 * @}
 */


#endif  /* __PJMEDIA_CONF_H__ */

