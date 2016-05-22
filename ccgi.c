/*
 * C CGI Library version 1.2
 *
 * Author:  Stephen C. Losen,  University of Virginia
 *
 * Copyright 2015 Stephen C. Losen.  Distributed under the terms
 * of the GNU Lesser General Public License (LGPL 2.1)
 *
 * C CGI is a C language library for parsing, decoding, storing and
 * retrieving data passed from a web browser to a CGI program.  C CGI
 * supports URL encoded form data (application/x-www-form-urlencoded)
 * and multipart (mutlipart/form-data), including file uploads. HTML
 * cookies can also be stored and retrieved.
 *
 * The library builds one or more "variable lists" of type
 * CGI_varlist.  A list entry consists of a name (null terminated
 * string) and one or more values (also null terminated strings)
 * associated with the name.  We allow multiple values because 1)
 * some HTML form elements (such as checkboxes and selections)
 * allow the form user to select multiple values and 2) different
 * form elements may be given the same name.
 *
 * The library function CGI_lookup_all() searches a variable list for
 * an entry with the specified name and returns the values in a
 * null terminated array of pointers to null terminated strings.
 *
 * Variable list entries can be obtained iteratively with
 * CGI_first_name(), CGI_next_name() and CGI_lookup_all().
 *
 * For a file upload, the user provides a filename template that
 * is passed to mkstemp() to create a new file to hold the data.
 */

#include <stdlib.h>
#include <unistd.h>
#include <ctype.h>
#include <string.h>
#include <stdarg.h>
#include <stdio.h>
#include <ccgi.h>

/* CGI_val is an entry in a list of variable values */

typedef struct CGI_val CGI_val;

struct CGI_val {
    CGI_val *next;        /* next entry on list */
    const char value[1];  /* variable value */
};

/*
 * CGI_varlist is an entry in a list of variables.  The fields
 * "iter" and "tail" are only used in the first list entry.
 */

struct CGI_varlist {
    CGI_varlist *next;      /* next entry on list */
    CGI_varlist *tail;      /* last entry on list */
    CGI_varlist *iter;      /* list iteration pointer */
    int numvalue;           /* number of values */
    CGI_val *value;         /* linked list of values */
    CGI_val *valtail;       /* last value on list */
    CGI_value *vector;      /* array of values */
    const char varname[1];  /* variable name */
};

/* strbuf is a string buffer of arbitrary size */

typedef struct {
    int size;
    char str[1];
}
strbuf;

/* mymalloc() is a malloc() wrapper that exits on failure */

static void *
mymalloc(int size) {
    void *ret = malloc(size);
    if (ret == 0) {
        fputs("C CGI Library out of memory\n", stderr);
        exit(1);
    }
    return ret;
}

/*
 * sb_get() creates or extends a strbuf
 */

static strbuf *
sb_get(strbuf *sb, int len) {
    int size;
    for (size = 128; size < len; size += size)
        ;
    if (sb == 0) {
        sb = (strbuf *) mymalloc(sizeof(*sb) + size);
    }
    else {
        sb = (strbuf *) realloc(sb, sizeof(*sb) + size);
        if (sb == 0) {
            fputs("C CGI Library out of memory\n", stderr);
            exit(1);
        }
    }
    sb->size = size;
    return sb;
}

/* savechar() saves a character in a strbuf at index idx */

static strbuf *
savechar(strbuf *sb, int idx, int c) {
    if (sb == 0 || idx >= sb->size) {
        sb = sb_get(sb, idx + 1);
    }
    sb->str[idx] = c;
    return sb;
}

/* savestr() saves a string in a strbuf */

static strbuf *
savestr(strbuf *sb, const char *str) {
    int len = strlen(str);
    if (sb == 0 || len >= sb->size) {
        sb = sb_get(sb, len + 1);
    }
    strcpy(sb->str, str);
    return sb;
}

/*
 * findvar() searches variable list "v" for an entry whose name
 * is "varname" and returns a pointer to the entry or else null
 * if not found.  If varname is null then we return v->iter,
 * which is the "current" variable entry.
 */

static CGI_varlist *
findvar(CGI_varlist *v, const char *varname) {
    if (varname == 0 && v != 0) {
        return v->iter;
    }
    for (; v != 0; v = v->next) {
        if (v->varname[0] == varname[0] &&
            strcmp(v->varname, varname) == 0)
        {
            break;
        }
    }
    return v;
}

/*
 * hex() returns the numeric value of hexadecimal digit "digit"
 * or returns -1 if "digit" is not a hexadecimal digit.
 */

static int
hex(int digit) {
    switch(digit) {

    case '0': case '1': case '2': case '3': case '4':
    case '5': case '6': case '7': case '8': case '9':
        return digit - '0';

    case 'A': case 'B': case 'C': case 'D': case 'E': case 'F':
        return 10 + digit - 'A';

    case 'a': case 'b': case 'c': case 'd': case 'e': case 'f':
        return 10 + digit - 'a';

    default:
        return -1;
    }
}

/* scanspaces() scans over spaces and tabs in a string */

static char *
scanspaces(char *p) {
    while (*p == ' ' || *p == '\t') {
        p++;
    }
    return p;
}

/*
 * scanattr() scans for an attribute such as name="value" or
 * name=value; saving the attribute name in attr[0] and saving
 * the attribute value in attr[1].  If successful, we return a
 * pointer to where the scan ended, otherwise we return null.
 * The input string is modified.
 */

static char *
scanattr(char *p, char *attr[2]) {
    int quote = 0;

    attr[0] = p = scanspaces(p);
    while (*p != '=' && *p != 0) {
        p++;
    }
    if (*p != '=' || p == attr[0]) {
        return 0;
    }
    *p++ = 0;
    if (*p == '"' || *p == '\'' || *p == '`') {
        quote = *p++;
    }
    attr[1] = p;
    if (quote != 0) {
        while(*p != quote && *p != 0) {
            p++;
        }
        if (*p != quote) {
            return 0;
        }
        *p++ = 0;
        if (*p == ';') {
            p++;
        }
    }
    else {
        while (*p != ';' && *p != ' ' && *p != '\t' &&
            *p != '\r' && *p != '\n' && *p != 0)
        {
            p++;
        }
        if (*p != 0) {
            *p++ = 0;
        }
    }
    return p;
}

/*
 * scanheader() scans a line of input for a header name followed by
 * a colon and the header value, such as:
 *
 * Content-Disposition: form-data;
 *
 * We place the header name in header[0] and the value in header[1].
 * If successful we return a pointer to the character where the scan
 * ended, otherwise we return null.  The input string is modified.
 */

static char *
scanheader(char *p, char *header[2]) {
    if (isalnum(*p) == 0) {
        return 0;
    }
    header[0] = p;
    while (*p != ':' && *p != 0) {
        p++;
    }
    if (*p != ':') {
        return 0;
    }
    *p++ = 0;
    header[1] = p = scanspaces(p);
    while (*p != ';' && *p != '\r' && *p != '\n' && *p != 0) {
        p++;
    }
    if (*p != 0) {
        *p++ = 0;
    }
    return p;
}

/*
 * readline() reads characters from open file "in" into strbuf "line".
 * We stop reading  when we encounter '\n' or end of file. We null
 * terminate "line" and return it.  We return null if we encounter
 * end of file without reading any characters.
 */

static strbuf *
readline(strbuf *line, FILE *in) {
    int c, i = 0;

    while ((c = getc(in)) != EOF) {
        line = savechar(line, i++, c);
        if (c == '\n') {
            break;
        }
    }
    if (i == 0) {
        if (line != 0) {
            free(line);
        }
        return 0;
    }
    return savechar(line, i, 0);  /* null terminate */
}

static strbuf *
copyvaluefast(const char *boundary, FILE *in, const int wantfile,
    strbuf *value, FILE *out)
{
    int i, k, matched;
    char c;
    char *buf, *obuf;
    size_t count;
    size_t n;
#define BUF_SZ 1048576
    matched = k = 0;
    buf = malloc(BUF_SZ);
    obuf = malloc(BUF_SZ);
    size_t ocount = 0;

    while (1) {
        count = fread(buf, 1, BUF_SZ, in);
        for (n = 0; n < count; n++) {
            c = buf[n];
            /*
             * If we partially match the boundary, then we copy the
             * entire matching prefix to the output.  We do not need to
             * backtrack and look for shorter matching prefixes because
             * they cannot exist.  The boundary always begins with '\r'
             * and never contains another '\r'.
             */

            if (matched > 0 && c != boundary[matched]) {
                for (i = 0; i < matched; i++) {
                    if (wantfile == 0) {
                        value = savechar(value, k++, boundary[i]);
                    }
                    else if (out != 0) {
                        obuf[ocount++] = boundary[i];
                        if (ocount >= BUF_SZ) {
                            if (fwrite(obuf, 1, BUF_SZ, out) != BUF_SZ)
                                exit(1);
                            else
                                ocount = 0;
                        }

                        //fputc(boundary[i], out);
                    }
                }
                matched = 0;
            }

            /* check for full or partial boundary match */

            if (c == boundary[matched]) {
                if (boundary[++matched] == 0) {
                    break;   /* full match */
                }
                continue;    /* partial match */
            }

            /* no match, so copy byte to output */

            if (wantfile == 0) {
                value = savechar(value, k++, c);
            }
            else if (out != 0) {
                obuf[ocount++] = c;
                if (ocount >= BUF_SZ) {
                    if (fwrite(obuf, 1, BUF_SZ, out) != BUF_SZ)
                        exit(1);
                    else
                        ocount = 0;
                }
            }
        }
        if (count < BUF_SZ) {
            if (ocount)
                fwrite(obuf, 1, ocount, out);

            break;
        }
    }
    if (wantfile == 0) {
        return savechar(value, k, 0);
    }
    return 0;
}

/*
 * read_multipart() reads form data encoded as "multipart/form-data"
 * and adds form field names and values to variable list "v" and
 * returns "v".
 *
 * The input is split into parts delimited by a boundary string.
 * Each part starts with this header:
 *
 * Content-Disposition: form-data; name="fieldname"; filename="filename"
 *
 * where "fieldname" is the name of the form field.  The "filename="
 * attribute is only present when this part contains file data for
 * a file upload.  A "Content-type:" header line may also be present
 * with a file upload.  After the header lines comes a blank line
 * followed by the field data (or file data) terminated by "\r\n--"
 * followed by the boundary string.  If the boundary string is
 * followed by "--\r\n" then this is the last part.
 */

static CGI_varlist *
read_multipart(CGI_varlist *v, const char *template) {
    const char *ctype, *name, *filename;
    char *p, *token[2], *boundary, *localname = 0;
    strbuf *bbuf = 0, *nbuf = 0, *fbuf = 0;
    strbuf *line = 0, *value = 0;
    int len, fd;
    FILE *out;

    /*
     * get the boundary string from the environment and prepend
     * "\r\n--"  to it.
     */

    if ((ctype = getenv("CONTENT_TYPE")) == 0 ||
        strncasecmp(ctype, "multipart/form-data;", len = 20) != 0)
    {
        return v;
    }
    bbuf = savestr(bbuf, ctype + len);
    if (scanattr(bbuf->str, token) == 0 ||
        strcasecmp(token[0], "boundary") != 0)
    {
        goto cleanup;
    }
    boundary = token[1] - 4;
    memcpy(boundary, "\r\n--", 4);

    /*
     * first line is the boundary string, but with "\r\n"
     * at the end rather than the start.
     */

    len = strlen(boundary) - 2;
    if ((line = readline(line, stdin)) == 0 ||
        strncmp(line->str, boundary + 2, len) != 0 ||
        line->str[len] != '\r' || line->str[len + 1] != '\n')
    {
        goto cleanup;
    }

    /* read all the parts */

    for (;;) {

        /* Scan header lines for the Content-Disposition: header */

        name = filename = 0;
        while ((line = readline(line, stdin)) != 0 &&
            (p = scanheader(line->str, token)) != 0)
        {
            if (strcasecmp(token[0], "Content-Disposition") != 0 ||
                strcasecmp(token[1], "form-data") != 0)
            {
                continue;
            }

            /* Content-Disposition: has field name and file name */

            while ((p = scanattr(p, token)) != 0) {
                if (name == 0 &&
                    strcasecmp(token[0], "name") == 0)
                {
                    nbuf = savestr(nbuf, token[1]);
                    name = nbuf->str;
                }
                else if (filename == 0 &&
                    strcasecmp(token[0], "filename") == 0)
                {
                    fbuf = savestr(fbuf, token[1]);
                    filename = fbuf->str;
                }
            }
        }

        /* after the headers is a blank line (just "\r\n") */

        if (line == 0 || name == 0 ||
            line->str[0] != '\r' || line->str[1] != '\n')
        {
            break;
        }

        /*
         * If filename is non null (file upload) then we read file data,
         * otherwise we read field data.  In either case the data
         * consists of everything up to, but not including the boundary.
         */

        if (filename != 0) {

            /* copy file data to newly created file */

            out = 0;
            if (template != 0 && *filename != 0) {
                if (localname == 0) {
                    localname = (char *) mymalloc(strlen(template) + 1);
                }
                strcpy(localname, template);
                if ((fd = mkstemp(localname)) >= 0) {
                    out = fdopen(fd, "wb");
                }
            }
            copyvaluefast(boundary, stdin, 1, 0, out);
            if (out != 0) {
                fclose(out);
                v = CGI_add_var(v, name, localname);
                v = CGI_add_var(v, name, filename);
            }
        }
        else {
            value = copyvaluefast(boundary, stdin, 0, value, 0);
            v = CGI_add_var(v, name, value->str);
        }

        /*
         * read the rest of the line after the boundary.  If we
         * get "--\r\n" then this is the last field.  Otherwise
         * we presumably get "\r\n" and we continue.
         */

        if ((line = readline(line, stdin)) != 0 &&
            line->str[0] == '-' && line->str[1] == '-' &&
            line->str[2] == '\r' && line->str[3] == '\n')
        {
            break;
        }
    }

cleanup:
    if (bbuf != 0) {
        free(bbuf);
    }
    if (nbuf != 0) {
        free(nbuf);
    }
    if (fbuf != 0) {
        free(fbuf);
    }
    if (line != 0) {
        free(line);
    }
    if (value != 0) {
        free(value);
    }
    if (localname != 0) {
        free(localname);
    }
    return v;
}

/*
 * EXPORTED FUNCTIONS
 *
 */
/*
 * CGI_add_var() adds a new variable name and value to variable list
 * "v" and returns the resulting list.  If "v" is null or if the
 * variable name is not on the list, then we create a new entry.
 * We add the value to the appropriate list entry.
 */

CGI_varlist *
CGI_add_var(CGI_varlist *v, const char *varname, const char *value) {
    CGI_val     *val;
    CGI_varlist *v2;

    if (varname == 0 || value == 0) {
        return v;
    }

    /* create a new value */

    val = (CGI_val *) mymalloc(sizeof(*val) + strlen(value));
    strcpy((char *) val->value, value);
    val->next = 0;

    /*
     * find the list entry or else create a new one.  Add the
     * new value.  We use "tail" pointers to keep the lists
     * in the same order as the input.
     */

    if ((v2 = findvar(v, varname)) == 0) {
        v2 = (CGI_varlist *) mymalloc(sizeof(*v2) + strlen(varname));
        strcpy((char *) v2->varname, varname);
        v2->value = val;
        v2->numvalue = 1;
        v2->next = v2->iter = v2->tail = 0;
        v2->vector = 0;
        if (v == 0) {
            v = v2;
        }
        else {
            v->tail->next = v2;
        }
        v->tail = v2;
    }
    else {
        v2->valtail->next = val;
        v2->numvalue++;
    }
    v2->valtail = val;
    if (v2->vector != 0) {
        free((void *)v2->vector);
        v2->vector = 0;
    }
    v->iter = 0;
    return v;
}

/*
 * CGI_decode_query() adds all the names and values in query string
 * "query" to variable list "v" (which may be null) and returns the
 * resulting variable list.  The query string has the form
 *
 * name1=value1&name2=value2&name3=value3
 *
 * We convert '+' to ' ' and convert %xx to the character whose
 * hex numeric value is xx.
 */

CGI_varlist *
CGI_decode_query(CGI_varlist *v, const char *query) {
    char *buf;
    const char *name, *value;
    int i, k, L, R, done;

    if (query == 0) {
        return v;
    }
    buf = (char *) mymalloc(strlen(query) + 1);
    name = value = 0;
    for (i = k = done = 0; done == 0; i++) {
        switch (query[i]) {

        case '=':
            if (name != 0) {
                break;  /* treat extraneous '=' as data */
            }
            if (name == 0 && k > 0) {
                name = buf;
                buf[k++] = 0;
                value = buf + k;
            }
            continue;

        case 0:
            done = 1;  /* fall through */

        case '&':
            buf[k] = 0;
            if (name == 0 && k > 0) {
                name = buf;
                value = buf + k;
            }
            if (name != 0) {
                v = CGI_add_var(v, name, value);
            }
            k = 0;
            name = value = 0;
            continue;

        case '+':
            buf[k++] = ' ';
            continue;

        case '%':
            if ((L = hex(query[i + 1])) >= 0 &&
                (R = hex(query[i + 2])) >= 0)
            {
                buf[k++] = (L << 4) + R;
                i += 2;
                continue;
            }
            break;  /* treat extraneous '%' as data */
        }
        buf[k++] = query[i];
    }
    free(buf);
    return v;
}

/*
 * CGI_get_cookie() adds all the cookie names and values from the
 * environment variable HTTP_COOKIE to variable list "v" (which
 * may be null) and returns the resulting variable list.
 */

CGI_varlist *
CGI_get_cookie(CGI_varlist *v) {
    const char *env;
    char *buf, *p, *cookie[2];

    if ((env = getenv("HTTP_COOKIE")) == 0) {
        return v;
    }
    buf = (char *) mymalloc(strlen(env) + 1);
    p = strcpy(buf, env);
    while ((p = scanattr(p, cookie)) != 0) {
        v = CGI_add_var(v, cookie[0], cookie[1]);
    }
    free(buf);
    return v;
}

/*
 * CGI_get_query() adds all the field names and values from the
 * environment variable QUERY_STRING to variable list "v" (which
 * may be null) and returns the resulting variable list.
 */

CGI_varlist *
CGI_get_query(CGI_varlist *v) {
    return CGI_decode_query(v, getenv("QUERY_STRING"));
}

/*
 * CGI_get_post() reads field names and values from stdin and adds
 * them to variable list "v" (which may be null) and returns the
 * resulting variable list.  We accept input encoded as
 * "application/x-www-form-urlencoded" or as "multipart/form-data".
 * In the case of a file upload, we write to a new file created
 * with mkstemp() and "template".  If the template is null or if
 * mkstemp() fails then we silently discard the uploaded file data.
 * The local name of the file (created by mkstemp()) and the remote
 * name (as specified by the user) can be obtained with
 * CGI_lookup_all(v, fieldname).
 */

CGI_varlist *
CGI_get_post(CGI_varlist *v, const char *template) {
    const char *env;
    char *buf;
    int len;

    if ((env = getenv("CONTENT_TYPE")) != 0 &&
        strncasecmp(env, "application/x-www-form-urlencoded", 33) == 0 &&
        (env = getenv("CONTENT_LENGTH")) != 0 &&
        (len = atoi(env)) > 0)
    {
        buf = (char *) mymalloc(len + 1);
        if (fread(buf, 1, len, stdin) == (size_t)len) {
            buf[len] = 0;
            v = CGI_decode_query(v, buf);
        }
        free(buf);
    }
    else {
        v = read_multipart(v, template);
    }
    return v;
}

/*
 * CGI_get_all() returns a variable list that contains a combination of the
 * following: cookie names and values from HTTP_COOKIE, field names and
 * values from QUERY_STRING, and POSTed field names and values from stdin.
 * File uploads are handled using "template" (see CGI_get_post())
 */

CGI_varlist *
CGI_get_all(const char *template) {
    CGI_varlist *v = 0;

    v = CGI_get_cookie(v);
    v = CGI_get_query(v);
    v = CGI_get_post(v, template);
    return v;
}

/* CGI_free_varlist() frees all memory used by variable list "v" */

void
CGI_free_varlist(CGI_varlist *v) {
    CGI_val *val, *valnext;

    if (v != 0) {
        if (v->vector != 0) {
            free((void *)v->vector);
        }
        for (val = v->value; val != 0; val = valnext) {
            valnext = val->next;
            free(val);
        }
        CGI_free_varlist(v->next);
        free(v);
    }
}


/*
 * CGI_lookup() searches variable list "v" for an entry named
 * "varname" and returns null if not found.  Otherwise we return the
 * first value associated with "varname", which is a null terminated
 * string.  If varname is null then we return the first value of the
 * "current entry", which was set using the iterating functions
 * CGI_first_name() and CGI_next_name().
 */

const char *
CGI_lookup(CGI_varlist *v, const char *varname) {
    return (v = findvar(v, varname)) == 0 ? 0 : v->value->value;
}

/*
 * CGI_lookup_all() searches variable list "v" for an entry named
 * "varname" and returns null if not found.  Otherwise we return
 * a pointer to a null terminated array of string pointers (see
 * CGI_value) where each string is a value of the variable.  If
 * varname is null then we return the values of the "current entry",
 * which was set using the iterating functions CGI_first_name() and
 * CGI_next_name().
 */

CGI_value *
CGI_lookup_all(CGI_varlist *v, const char *varname) {
    CGI_val *val;
    int i;

    if ((v = findvar(v, varname)) == 0) {
        return 0;
    }
    if (v->vector == 0) {
        v->vector = (CGI_value *)
            mymalloc(sizeof(CGI_value) * (v->numvalue + 1));
        i = 0;

        /* to initialize v->vector we must cast away const */

        for (val = v->value; val != 0 && i < v->numvalue;
            val = val->next)
        {
            ((const char **)v->vector)[i++] = val->value;
        }
        ((const char **)v->vector)[i] = 0;
    }
    return v->vector;
}

/*
 * CGI_first_name() returns the name of the first entry in
 * variable list "v", or null if "v" is null.
 */

const char *
CGI_first_name(CGI_varlist *v) {
    return v == 0 ? 0 : (v->iter = v)->varname;
}

/*
 * CGI_next_name() returns the name of the next entry in variable list
 * "v" after the most recent call to CGI_first_name() or CGI_next_name().
 * We return null if there are no more entries
 */

const char *
CGI_next_name(CGI_varlist *v) {
    return v == 0 || v->iter == 0 || (v->iter = v->iter->next) == 0 ?
        0 : v->iter->varname;
}
