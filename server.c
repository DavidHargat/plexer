#include <stdio.h>
#include <stdlib.h>

#include <string.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>

#include "server.h"
#include "file.h"
#include "socket.h"
#include "http.h"
#include "connection.h"
#include "log.h"

static const char *plxr_html_404 =
"<html><head></head><body><h1>file not found</h1></body></html>";

static const char *plxr_html_500 =
"<html><head></head><body><h1>internal server error</h1></body></html>";

const char *
extension_from_path(char *path)
{
	char *ptr = path;

	/* skip to the end */
	while (*ptr != '\0') {
		ptr += 1;
	}

	while (ptr > path) {
		ptr -= 1;
		if (*ptr == '.') {
			return ptr+1;
		}
	}

	return NULL;
}

#define ENTRY(a,b) \
	(const char *[2]){a, b}

static const char **extension_table[] =
{
	ENTRY("au", "audio/basic"),
	ENTRY("avi", "video/avi"),
	ENTRY("bmp", "image/bmp"),
	ENTRY("bz2", "application/x-bzip2"),
	ENTRY("css", "text/css"),
	ENTRY("dtd", "application/xml-dtd"),
	ENTRY("doc", "application/msword"),
	ENTRY("exe", "application/octet-stream"),
	ENTRY("gif", "image/gif"),
	ENTRY("gz", "application/x-gzip"),
	ENTRY("hqx", "application/mac-binhex40"),
	ENTRY("html", "text/html"),
	ENTRY("jar", "application/java-archive"),
	ENTRY("jpg", "image/jpeg"),
	ENTRY("js", "application/x-javascript"),
	ENTRY("midi", "audio/x-midi"),
	ENTRY("mp3", "audio/mpeg"),
	ENTRY("mpeg", "video/mpeg"),
	ENTRY("ogg", "application/ogg"),
	ENTRY("pdf", "application/pdf"),
	ENTRY("pl", "application/x-perl"),
	ENTRY("png", "image/png"),
	ENTRY("ppt", "application/vnd.ms-powerpoint"),
	ENTRY("ps", "application/postscript"),
	ENTRY("qt", "video/quicktime"),
	ENTRY("ra", "audio/vnd.rn-realaudio"),
	ENTRY("ram", "audio/vnd.rn-realaudio"),
	ENTRY("rdf", "application/rdf"),
	ENTRY("rtf", "application/rtf"),
	ENTRY("sgml", "text/sgml"),
	ENTRY("sit", "application/x-stuffit"),
	ENTRY("svg", "image/svg+xml"),
	ENTRY("swf", "application/x-shockwave-flash"),
	ENTRY("tar.gz", "application/x-tar"),
	ENTRY("tgz", "application/x-tar"),
	ENTRY("tiff", "image/tiff"),
	ENTRY("tsv", "text/tab-separated-values"),
	ENTRY("txt", "text/plain"),
	ENTRY("c", "text/plain"),
	ENTRY("h", "text/plain"),
	ENTRY("md", "text/plain"),
	ENTRY("wav", "audio/wav"),
	ENTRY("xls", "application/vnd.ms-excel"),
	ENTRY("xml", "application/xml"),
	ENTRY("zip", "application/zip"),
	NULL
};
#undef ENTRY

const char *
content_type_from_path(char *path)
{
	const char *ext;
	const char **entry;
	size_t i;

	ext = extension_from_path(path);

	if (ext != NULL) {
		for (i = 0; (entry = extension_table[i]); i++) {
			if (strcmp(entry[0], ext) == 0) {
				return entry[1];
			}
		}
	}

	return "text/plain"; /* if there's no extension, assume it's text. */
}

int
plxr_serve_data(
	struct plxr_connection *conn,
	int status_code,
	const char *content_type,
	const char *data,
	size_t data_len)
{
	char headers[4096] = {0};
	size_t headers_len;
	int ret;

	headers_len = plxr_http_response(
		headers, sizeof(headers), status_code, content_type, data_len);

	if (headers_len > sizeof(headers))
	{
		WARN("out of memory");
		return -1;
	}

	ret = plxr_socket_write_timeout(conn->fd, headers, headers_len, 250);
	if (ret != headers_len)
	{
		WARN("plxr_socket_write_timeout: failed");
		return -1;
	}

	conn->resp_len += ret;

	ret = plxr_socket_write_timeout(conn->fd, data, data_len, 250);
	if (ret != data_len)
	{
		WARN("plxr_socket_write_timeout: failed");
		return -1;
	}

	conn->resp_len += ret;

	conn->resp_status_code = status_code;
	conn->resp_timestamp   = time(NULL);

	return 0;
}

int
plxr_serve_file(
	struct plxr_connection *conn,
	int status_code,
	char *filename)
{
	char *data;
	off_t data_len;
	int ret;

	data = plxr_alloc_file(filename, &data_len); /* dynamically allocated */
	if (data == NULL)
	{
		WARN("plx_alloc_file: failed");
		return -1;
	}

	ret = plxr_serve_data(
		conn, status_code, content_type_from_path(filename), data, data_len);

	free(data); /* don't leak memory */

	return ret;
}

int
plxr_serve_string(
	struct plxr_connection *conn,
	int status_code,
	const char *content_type,
	const char *str)
{
	return plxr_serve_data(conn, status_code, content_type, str, strlen(str));
}

static char path_buffer[1024];
char *
compose_path(char *path, char *filename)
{
	bzero(path_buffer, sizeof(path_buffer));
	if (
		snprintf(
			path_buffer,
			sizeof(path_buffer),
			strlen(path) == 0 ?
				"%s%s" :
				"%s/%s",
			path,
			filename) > sizeof(path_buffer)
	) {
		WARN("out of memory");
		return NULL;
	}
	else
		return path_buffer;
}

int
plxr_render_dir_html(char *dest, size_t size, char *path, DIR *dir, char *host)
{
	const char *page_template =
		"<!doctype html>"
		"<html><head></head>"
		"<body><ul>%s</ul></body></html>"
	;

	const char *li_template =
		"<li><a href=\"http://%s/%s\">%s</a></li>"
	;

	struct dirent *ent;
	char li_buffer[4096] = {0};
	size_t count = 0;

	while (( ent = readdir(dir) ))
	{
		count += snprintf(
			&li_buffer[count],
			sizeof(li_buffer) - count,
			li_template,
			host,
			compose_path(path, ent->d_name),
			ent->d_name);

		if (count > sizeof(li_buffer))
		{
			WARN("out of memory");
			return -1;
		}
	}

	return snprintf(dest, size, page_template, li_buffer);
}

int
plxr_serve_dir(struct plxr_connection *conn, char *path, DIR *dir)
{
	char data[4096];
	int data_len;
	char *host;

	host = plxr_http_header(&conn->request, "Host");
	if (host == NULL)
	{
		WARN("request missing `Host` header");
		return -1; /* NOTE: maybe we should 400 here */
	}

	data_len = plxr_render_dir_html(data, sizeof(data), path, dir, host);

	if (data_len < 0)
	{
		WARN("plxr_render_dir_html: failed");
		return -1;
	}
	if (data_len > sizeof(data))
	{
		WARN("out of memory");
		return -1;
	}

	return plxr_serve_data(conn, 200, "text/html", data, data_len);
}

char *
uri_normalize(char *uri)
{
	char *ptr;

	/* cut preceding slashes */
	while (*uri == '/') {
		uri += 1;
	}

	/* skip to the end */
	ptr = uri;
	while (*ptr != '\0') {
		ptr += 1;
	}

	/* sweep up the trailing slashes, excluding the first */
	while (--ptr > (uri + 1)) {
		if (*ptr == '/') {
			*ptr = '\0';
		} else {
			break;
		}
	}

	return uri;
}

char *
plxr_append_strings(char *a, char *b)
{
	static char buf[512];
	bzero(buf, sizeof(buf));

	if (snprintf(buf, sizeof(buf), "%s%s", a, b) > sizeof(buf))
	{
		WARN("out of memory");
		return NULL; /* out of memory */
	}

	return buf;
}

/* Takes a URI and figures out a filepath
 * to serve, or NULL for 404.
 */
char *
plxr_router(int *filetype, char *uri)
{
	char *filename;

	if (uri == NULL) {
		*filetype = PLX_FILE_NOT_FOUND;
		return NULL;
	}

	/* since "/" was cut, empty string here means "/", which we map to the current directory */
	if (strcmp(uri, "") == 0)
		uri = ".";

	*filetype = plxr_stat(uri);
	switch (*filetype)
	{

	case PLX_FILE_REG:
		return uri;

	case PLX_FILE_DIR:

		/* if it's a directory, check for an index.html */
		if (strcmp(uri, ".") == 0)
			filename = "index.html";
		else
			filename = plxr_append_strings(uri, "/index.html");

		if (filename &&
			(plxr_stat(filename) == PLX_FILE_REG)) {
			*filetype = PLX_FILE_REG;
			printf("index=");
			return filename; /* found an index.html in this folder */
		}
		return uri;

	case PLX_FILE_ERR:
	case PLX_FILE_NOT_FOUND:
		break;
	}

	return NULL;
}

/* TODO this is the messiest function, would be nice to clean it up
 * with some better url parsing.
 */
int
plxr_serve_file_or_dir(struct plxr_connection *conn)
{
	DIR *dir;
	char *path;
	char buf[256] = {0};
	char *uri;
	int ret;
	int filetype;

	/* Buffer for processing the uri. */
	strncpy(buf, conn->request.uri, sizeof(buf));

	/* Clean up preceding and trailing slashes (not part of the spec, just a design choice). */
	uri = uri_normalize(buf);

	/* Map the uri to a file path, if one is found. */
	path = plxr_router(&filetype, uri);

	switch (filetype)
	{
	case PLX_FILE_REG:
		return plxr_serve_file(conn, 200, path);

	case PLX_FILE_DIR:
		/* open up the directory and render it into an html page */
		if ((dir = opendir(path)) == NULL) {
			ERROR("opendir");
			return -1;
		}
		ret = plxr_serve_dir(conn, path, dir);
		closedir(dir);
		return ret;

	case PLX_FILE_NOT_FOUND:
		return plxr_serve_string(conn, 404, "text/html", plxr_html_404);

	case PLX_FILE_ERR: /* fallthrough */
	default:
		break;
	}
	return plxr_serve_string(conn, 500, "text/html", plxr_html_500);
}

