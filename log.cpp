/**************************************************************

   log.cpp - Simple logging for Switchres

   ---------------------------------------------------------

   Switchres   Modeline generation engine for emulation

   License     GPL-2.0+
   Copyright   2010-2020 Chris Kennedy, Antonio Giner,
                         Alexandre Wodarczyk, Gil Delescluse

 **************************************************************/

#include "log.h"

enum log_verbosity { NONE, ERROR, INFO, DEBUG };
static log_verbosity log_level = INFO;

void log_dummy(const char *, ...) {}

LOG_VERBOSE log_verbose = &log_dummy;
LOG_INFO log_info = &log_dummy;
LOG_ERROR log_error = &log_dummy;

void set_log_verbose(void *func_ptr)
{
	if (log_level >= DEBUG)
		log_verbose = (LOG_VERBOSE)func_ptr;
}

void set_log_info(void *func_ptr)
{
	if (log_level >= INFO)
		log_info = (LOG_INFO)func_ptr;
}

void set_log_error(void *func_ptr)
{
	if (log_level >= ERROR)
		log_error = (LOG_ERROR)func_ptr;
}

void set_log_verbosity(int level)
{
	// Keep the log in the enum bounds
	if (level < NONE)
		level = NONE;
	if(level > DEBUG)
		level = DEBUG;
	log_level = (log_verbosity)level;

	// Reinit loggers
	if (log_level < DEBUG)
		log_verbose = &log_dummy;
	if (log_level < INFO)
		log_info = &log_dummy;
	if (log_level < ERROR)
		log_error = &log_dummy;
}
