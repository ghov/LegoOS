/*
 * Copyright (c) 2016-2017 Wuklab, Purdue University. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef _LEGO_UAPI_FIT_H_
#define _LEGO_UAPI_FIT_H_

enum lego_manager_type{
	LEGO_PROCESSOR,
	LEGO_MEMORY,
	LEGO_STORAGE,
};

#define FIT_NAME_MAX	16

struct fit_machine_info {
	const char		hostname[FIT_NAME_MAX];
	unsigned long		lid;
	unsigned int		first_qpn;
	enum lego_manager_type	type;
};

#endif /* _LEGO_UAPI_FIT_H_ */