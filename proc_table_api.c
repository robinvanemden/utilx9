/***************************************************************************
 * Copyright (C) 2017 - 2020, Lanka Hsu, <lankahsu@gmail.com>, et al.
 *
 * This software is licensed as described in the file COPYING, which
 * you should have received as part of this distribution.
 *
 * You may opt to use, copy, modify, merge, publish, distribute and/or sell
 * copies of the Software, and permit persons to whom the Software is
 * furnished to do so, under the terms of the COPYING file.
 *
 * This software is distributed on an "AS IS" basis, WITHOUT WARRANTY OF ANY
 * KIND, either express or implied.
 *
 ***************************************************************************/
#include "utilx9.h"

CLIST(ProcListHead);

ProcList_t *proc_entry_push(clist_t head, const char *name)
{
	ProcList_t *proc_entry = (ProcList_t*)SAFE_CALLOC(1, sizeof(ProcList_t));
	proc_entry->name = name;

	clist_push(head, proc_entry);
	
	return proc_entry;
}

void proc_entry_del(clist_t head, ProcList_t *proc_entry)
{
	if (proc_entry)
	{
		clist_remove(head, proc_entry);
		SAFE_FREE(proc_entry);
	}
}

static ProcList_t *proc_entry_search(clist_t head, char *proc_name)
{
	ProcList_t *cur = NULL;

	for (cur = clist_head(head); cur != NULL; cur = clist_item_next(cur))
	{
		//DBG_IF_LN("(proc_name: %s, name: %s)", proc_name, (char*)cur->name);
		if (SAFE_STRSTR(proc_name, (char*)cur->name))
		{
			return cur;
		}
	}

	return NULL;
}

static void proc_entry_cpuusage(clist_t head)
{
	CPUInfo_t cpuinfo;

	memset(&cpuinfo, 0, sizeof(CPUInfo_t));
	sys_cpu_info(&cpuinfo);

	ProcList_t *cur;
	for (cur = clist_head(head); cur != NULL; cur = clist_item_next(cur))
	{
		ProcInfo_t *procinfo_ctx = &cur->procinfo;
		if (procinfo_ctx->pid!=0)
		{
			proc_cpu_info(procinfo_ctx);
			procinfo_ctx->cpu_usage = 0.0;
		}
	}

	usleep(200000);

	sys_cpu_info(&cpuinfo);
	for (cur = clist_head(head); cur != NULL; cur = clist_item_next(cur))
	{
		ProcInfo_t *procinfo_ctx = &cur->procinfo;
		if (procinfo_ctx->pid!=0)
		{
			proc_cpu_info(procinfo_ctx);

			if ( 0 != cpuinfo.duration)
			{ 
				procinfo_ctx->cpu_usage = 100.0 * (procinfo_ctx->duration)/(cpuinfo.duration);
			}
		}
	}
}

void proc_entry_reset(clist_t head)
{
	ProcList_t *cur = NULL;
	for (cur = clist_head(head); cur != NULL; cur = clist_item_next(cur))
	{
		ProcInfo_t *procinfo_ctx = &cur->procinfo;
		procinfo_ctx->pid = 0;
	}
}

void proc_entry_scan(clist_t head)
{
	DIR* dir;
	struct dirent* ent;
	char* endptr;

	if (!(dir = opendir("/proc")))
	{
		DBG_ER_LN("opendir error !!! (/proc)");
		return;
	}

	while((ent = readdir(dir)) != NULL)
	{
		// if endptr is not a null character, the directory is not entirely numeric, so ignore it
		char cmdline[LEN_OF_CMDLINE] = "";
		unsigned long pid = strtol(ent->d_name, &endptr, 10);
		if (*endptr != '\0')
		{
			continue;
		}

		/* try to open the cmdline file */
		SAFE_SNPRINTF(cmdline, (int)sizeof(cmdline), "/proc/%ld/cmdline", pid);
		FILE* fp = SAFE_FOPEN(cmdline, "r");
		if (fp) 
		{
			if (SAFE_FGETS(cmdline, sizeof(cmdline), fp) != NULL)
			{
				// check the first token in the file, the program name
				char *saveptr = NULL;
				char *first = SAFE_STRTOK_R(cmdline, " ", &saveptr);
				ProcList_t *proc_entry = proc_entry_search(head, first);
				if ( proc_entry )
				{
					proc_entry->procinfo.pid = pid;
					ProcInfo_t *procinfo_ctx = &proc_entry->procinfo;
					proc_info_static(procinfo_ctx);
				}
			}
			SAFE_FCLOSE(fp);
		}
	}

	closedir(dir);
}

void proc_entry_print_ex(clist_t head, int fdlist)
{
	ProcList_t *cur = NULL;
	for (cur = clist_head(head); cur != NULL; cur = clist_item_next(cur))
	{
		ProcInfo_t *procinfo_ctx = &cur->procinfo;

		//DBG_LN_Y("%s (pid: %ld, name: %s, size: %ld, resident: %ld, cpu_usage: %d, fdSize: %ld, fdcount: %ld)", cur->name, procinfo_ctx->pid, procinfo_ctx->name, procinfo_ctx->size, procinfo_ctx->resident, (int)procinfo_ctx->cpu_usage, procinfo_ctx->fdsize, procinfo_ctx->fdcount );
		DBG_LN_Y("%5ld:%-20s (VmSize: %6ld, VmRSS: %6ld, shared: %6ld, fdcount: %3ld)", procinfo_ctx->pid, cur->name, procinfo_ctx->size, procinfo_ctx->resident, procinfo_ctx->shared, procinfo_ctx->fdcount );
		if (fdlist)
		{
			int i = 0;
			for (i=0; i<procinfo_ctx->fdcount; i++)
			{
			  DBG_LN_Y(" (fd: %d/%ld -> %s)", procinfo_ctx->fdinfo[i].fd, procinfo_ctx->fdcount, procinfo_ctx->fdinfo[i].slink);
			}
		}
	}
}

clist_t proc_table_head(void)
{
	return ProcListHead;
}

void proc_entry_print(int fdlist)
{
	proc_entry_print_ex( proc_table_head(), fdlist);
}

void proc_table_add_philio(clist_t head)
{
	proc_entry_push(head, "app");
	proc_entry_push(head, "debuger");
	proc_entry_push(head, "platform");

	// network
	proc_entry_push(head, "orbwebm2m");
	proc_entry_push(head, "icloud");

	// 1.x
	proc_entry_push(head, "philio-sdk");
	
	// 2.x
	proc_entry_push(head, "pan27");
	proc_entry_push(head, "sdk");
	proc_entry_push(head, "storage");
	proc_entry_push(head, "zigbee");
	proc_entry_push(head, "zwave");
	//proc_entry_push(head, "zwared");
}

void proc_table_free(clist_t head)
{
	clist_free(head);
}

void proc_table_open(void)
{
	clist_init(proc_table_head());
	proc_table_add_philio(proc_table_head());
}

void proc_table_refresh(void)
{
	proc_entry_reset(proc_table_head());
	proc_entry_scan(proc_table_head());
	proc_entry_cpuusage(proc_table_head());
}

void proc_table_close(void)
{
	proc_table_free(proc_table_head());
}
