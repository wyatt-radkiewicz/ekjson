/**
 * Common files and functions used to setup example code
 * (loading files basically)
 */
#ifndef _common_h_
#define _common_h_

#include <stdio.h>
#include <stdlib.h>

/**
 * \brief loads a file and returns the contents as a string
 * \param path path to a file
 * \returns file contents (null-terminated) or NULL if we can't open the file
 */
static char *file_load_str(const char *path) {
	// Try opening file
	FILE *fp = fopen(path, "rb");
	if (!fp) return NULL;

	// Get file length and allocate room for string
	fseek(fp, 0, SEEK_END);	
	const long len = ftell(fp);
	if (!len) return NULL;
	char *str = malloc(len + 1);

	// Read in the text and append null-terminator
	fseek(fp, 0, SEEK_SET);
	fread(str, 1, len, fp);
	str[len] = '\0';

	// Close the file handle and return
	fclose(fp);
	return str;
}

#endif

