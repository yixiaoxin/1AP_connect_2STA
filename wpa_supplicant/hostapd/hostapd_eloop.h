/*
 * Event loop
 * Copyright (c) 2002-2006, Jouni Malinen <j@w1.fi>
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 *
 * This file defines an event loop interface that supports processing events
 * from registered timeouts (i.e., do something after N seconds), sockets
 * (e.g., a new packet available for reading), and signals. eloop.c is an
 * implementation of this interface using select() and sockets. This is
 * suitable for most UNIX/POSIX systems. When porting to other operating
 * systems, it may be necessary to replace that implementation with OS specific
 * mechanisms.
 */

#ifndef HOSTAPD_ELOOP_H
#define HOSTAPD_ELOOP_H

#if 0
/**
 * ELOOP_ALL_CTX - eloop_cancel_timeout() magic number to match all timeouts
 */
#define ELOOP_ALL_CTX (void *) -1

/**
 * eloop_event_type - eloop socket event type for eloop_register_sock()
 * @EVENT_TYPE_READ: Socket has data available for reading
 * @EVENT_TYPE_WRITE: Socket has room for new data to be written
 * @EVENT_TYPE_EXCEPTION: An exception has been reported
 */
typedef enum {
	EVENT_TYPE_READ = 0,
	EVENT_TYPE_WRITE,
	EVENT_TYPE_EXCEPTION
} eloop_event_type;

/**
 * eloop_sock_handler - eloop socket event callback type
 * @sock: File descriptor number for the socket
 * @eloop_ctx: Registered callback context data (eloop_data)
 * @sock_ctx: Registered callback context data (user_data)
 */
typedef void (*eloop_sock_handler)(int sock, void *eloop_ctx, void *sock_ctx);

/**
 * eloop_event_handler - eloop generic event callback type
 * @eloop_ctx: Registered callback context data (eloop_data)
 * @user_ctx: Registered callback context data (user_data)
 */
typedef void (*eloop_event_handler)(void *eloop_ctx, void *user_ctx);

/**
 * eloop_timeout_handler - eloop timeout event callback type
 * @eloop_ctx: Registered callback context data (eloop_data)
 * @user_ctx: Registered callback context data (user_data)
 */
typedef void (*eloop_timeout_handler)(void *eloop_ctx, void *user_ctx);

/**
 * eloop_signal_handler - eloop signal event callback type
 * @sig: Signal number
 * @signal_ctx: Registered callback context data (user_data from
 * eloop_register_signal(), eloop_register_signal_terminate(), or
 * eloop_register_signal_reconfig() call)
 */
typedef void (*eloop_signal_handler)(int sig, void *signal_ctx);
#endif
/**
 * eloop_init() - Initialize global event loop data
 * Returns: 0 on success, -1 on failure
 *
 * This function must be called before any other eloop_* function.
 */
int hostapd_eloop_init(void);

/**
 * eloop_register_read_sock - Register handler for read events
 * @sock: File descriptor number for the socket
 * @handler: Callback function to be called when data is available for reading
 * @eloop_data: Callback context data (eloop_ctx)
 * @user_data: Callback context data (sock_ctx)
 * Returns: 0 on success, -1 on failure
 *
 * Register a read socket notifier for the given file descriptor. The handler
 * function will be called whenever data is available for reading from the
 * socket. The handler function is responsible for clearing the event after
 * having processed it in order to avoid eloop from calling the handler again
 * for the same event.
 */
int hostapd_eloop_register_read_sock(int sock, eloop_sock_handler handler,
			     void *eloop_data, void *user_data);

/**
 * eloop_unregister_read_sock - Unregister handler for read events
 * @sock: File descriptor number for the socket
 *
 * Unregister a read socket notifier that was previously registered with
 * eloop_register_read_sock().
 */
void hostapd_eloop_unregister_read_sock(int sock);


/**
 * eloop_register_timeout - Register timeout
 * @secs: Number of seconds to the timeout
 * @usecs: Number of microseconds to the timeout
 * @handler: Callback function to be called when timeout occurs
 * @eloop_data: Callback context data (eloop_ctx)
 * @user_data: Callback context data (sock_ctx)
 * Returns: 0 on success, -1 on failure
 *
 * Register a timeout that will cause the handler function to be called after
 * given time.
 */
int hostapd_eloop_register_timeout(unsigned int secs, unsigned int usecs,
			   eloop_timeout_handler handler,
			   void *eloop_data, void *user_data);

/**
 * eloop_cancel_timeout - Cancel timeouts
 * @handler: Matching callback function
 * @eloop_data: Matching eloop_data or %ELOOP_ALL_CTX to match all
 * @user_data: Matching user_data or %ELOOP_ALL_CTX to match all
 * Returns: Number of cancelled timeouts
 *
 * Cancel matching <handler,eloop_data,user_data> timeouts registered with
 * eloop_register_timeout(). ELOOP_ALL_CTX can be used as a wildcard for
 * cancelling all timeouts regardless of eloop_data/user_data.
 */
int hostapd_eloop_cancel_timeout(eloop_timeout_handler handler,
			 void *eloop_data, void *user_data);

/**
 * eloop_cancel_timeout_one - Cancel a single timeout
 * @handler: Matching callback function
 * @eloop_data: Matching eloop_data
 * @user_data: Matching user_data
 * @remaining: Time left on the cancelled timer
 * Returns: Number of cancelled timeouts
 *
 * Cancel matching <handler,eloop_data,user_data> timeout registered with
 * eloop_register_timeout() and return the remaining time left.
 */
int hostapd_eloop_cancel_timeout_one(eloop_timeout_handler handler,
			     void *eloop_data, void *user_data,
			     struct os_reltime *remaining);

/**
 * eloop_is_timeout_registered - Check if a timeout is already registered
 * @handler: Matching callback function
 * @eloop_data: Matching eloop_data
 * @user_data: Matching user_data
 * Returns: 1 if the timeout is registered, 0 if the timeout is not registered
 *
 * Determine if a matching <handler,eloop_data,user_data> timeout is registered
 * with eloop_register_timeout().
 */
int hostapd_eloop_is_timeout_registered(eloop_timeout_handler handler,
				void *eloop_data, void *user_data);

/**
 * eloop_deplete_timeout - Deplete a timeout that is already registered
 * @req_secs: Requested number of seconds to the timeout
 * @req_usecs: Requested number of microseconds to the timeout
 * @handler: Matching callback function
 * @eloop_data: Matching eloop_data
 * @user_data: Matching user_data
 * Returns: 1 if the timeout is depleted, 0 if no change is made, -1 if no
 * timeout matched
 *
 * Find a registered matching <handler,eloop_data,user_data> timeout. If found,
 * deplete the timeout if remaining time is more than the requested time.
 */
int hostapd_eloop_deplete_timeout(unsigned int req_secs, unsigned int req_usecs,
			  eloop_timeout_handler handler, void *eloop_data,
			  void *user_data);

/**
 * eloop_replenish_timeout - Replenish a timeout that is already registered
 * @req_secs: Requested number of seconds to the timeout
 * @req_usecs: Requested number of microseconds to the timeout
 * @handler: Matching callback function
 * @eloop_data: Matching eloop_data
 * @user_data: Matching user_data
 * Returns: 1 if the timeout is replenished, 0 if no change is made, -1 if no
 * timeout matched
 *
 * Find a registered matching <handler,eloop_data,user_data> timeout. If found,
 * replenish the timeout if remaining time is less than the requested time.
 */
int hostapd_eloop_replenish_timeout(unsigned int req_secs, unsigned int req_usecs,
			    eloop_timeout_handler handler, void *eloop_data,
			    void *user_data);

/**
 * eloop_sock_requeue - Requeue sockets
 *
 * Requeue sockets after forking because some implementations require this,
 * such as epoll and kqueue.
 */
int hostapd_eloop_sock_requeue(void);

/**
 * eloop_run - Start the event loop
 *
 * Start the event loop and continue running as long as there are any
 * registered event handlers. This function is run after event loop has been
 * initialized with event_init() and one or more events have been registered.
 */
void hostapd_eloop_run(void);

/**
 * eloop_terminate - Terminate event loop
 *
 * Terminate event loop even if there are registered events. This can be used
 * to request the program to be terminated cleanly.
 */
void hostapd_eloop_terminate(void);

/**
 * eloop_destroy - Free any resources allocated for the event loop
 *
 * After calling eloop_destroy(), other eloop_* functions must not be called
 * before re-running eloop_init().
 */
void hostapd_eloop_destroy(void);

/**
 * eloop_terminated - Check whether event loop has been terminated
 * Returns: 1 = event loop terminate, 0 = event loop still running
 *
 * This function can be used to check whether eloop_terminate() has been called
 * to request termination of the event loop. This is normally used to abort
 * operations that may still be queued to be run when eloop_terminate() was
 * called.
 */
int hostapd_eloop_terminated(void);


#endif /* HOSTAPD_ */
