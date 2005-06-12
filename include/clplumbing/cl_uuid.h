#ifndef _CL_UUID_H_
#define _CL_UUID_H_

typedef struct cl_uuid_s{	
	unsigned char	uuid[16];
}cl_uuid_t;

void cl_uuid_copy(cl_uuid_t* dst, cl_uuid_t* src);
void cl_uuid_clear(cl_uuid_t* uu);
int cl_uuid_compare(const cl_uuid_t* uu1, const cl_uuid_t* uu2);
void cl_uuid_generate(cl_uuid_t* out);
int cl_uuid_is_null(cl_uuid_t* uu);
int cl_uuid_parse( char *in, cl_uuid_t* uu);
void cl_uuid_unparse(cl_uuid_t* uu, char *out);


#endif


