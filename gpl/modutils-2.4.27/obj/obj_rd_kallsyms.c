/* Build a section containing all non-stack symbols.
   Copyright 2000 Keith Owens <kaos@ocs.com.au>

   This file is part of the Linux modutils.

   This program is free software; you can redistribute it and/or modify it
   under the terms of the GNU General Public License as published by the
   Free Software Foundation; either version 2 of the License, or (at your
   option) any later version.

   This program is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software Foundation,
   Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.  */

#include <stdlib.h>
#include <string.h>
#include <malloc.h>

#include "obj.h"
#include "kallsyms.h"
#include "util.h"

/*======================================================================*/

void print_kallsyms_hdr( struct kallsyms_header *k_hdr) {
    if (k_hdr) {
	printf("size=%d\n", k_hdr->size);
	printf("total size=%d\n", k_hdr->total_size);
	printf("sections=%d\n", k_hdr->sections);
	printf("sections off=%d\n", k_hdr->section_off);
	printf("symbols =%d\n", k_hdr->symbols);
	printf("symbols off =%d\n", k_hdr->symbol_off);
	printf("symbols sz =%d\n", k_hdr->symbol_size);
    }
}
void print_kallsyms_section( struct kallsyms_section *sec) {
    if (sec) {
	printf("start=%d\n", sec->start);
	printf("size=%d\n", sec->size);
	printf("name_off=%d\n", sec->name_off);
	printf("flags=0x%x\n", sec->flags);
    }
}
void print_kallsyms_symbol( struct kallsyms_header *hdr, struct kallsyms_symbol *sym) {
	char *names = (char*) hdr;
	names += hdr->string_off +sym->name_off;

	printf("0x%08x %s\n", sym->symbol_addr, names);
}

int
obj_rd_kallsyms (struct obj_file *fin, struct obj_file **fout_result)
{
    int i;
    int ret = -1;
    struct kallsyms_header	*k_hdr;
    struct obj_section		*sec = NULL;
    struct kallsyms_symbol	*k_sym;
    int	n_sec = fin->header.e_shnum;

    for (i = 0 ; i < n_sec ; i++) {
	if (strcmp(KALLSYMS_SEC_NAME,fin->sections[i]->name)==0) {
	    sec = fin->sections[i];
	    break;
	}
    }
    if (sec == NULL)
	goto out;

    // print_kallsyms_hdr( k_hdr);

#ifdef SWAP_ENDIAN
    // extern void swap_kallsyms_section(struct kallsyms_header *);
    swap_kallsyms_hdr((struct kallsyms_header *) sec->contents);
    swap_kallsyms_sections((struct kallsyms_header *) sec->contents);
    swap_kallsyms_symbols((struct kallsyms_header *) sec->contents);
#endif
    k_hdr = (struct kallsyms_header*) sec->contents;
    k_sym = (struct kallsyms_symbol*) (((char*) k_hdr) + k_hdr->symbol_off);

    for (i=0; i < k_hdr->symbols ; i++, k_sym++)
    {
	    print_kallsyms_symbol(k_hdr, k_sym);
    }
    ret = 0;

out:

    return ret;
}
