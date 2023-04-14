/*
 * src/tutorial/complex.c
 *
 ******************************************************************************
  This file contains routines that can be bound to a Postgres backend and
  called by the backend in the process of processing queries.  The calling
  format for these routines is dictated by Postgres architecture.
******************************************************************************/
 
#include "postgres.h"
#include <math.h>
#include "fmgr.h"
#include "libpq/pqformat.h"		/* needed for send/recv functions */
#include "common/shortest_dec.h"
 
PG_MODULE_MAGIC;
 
typedef struct Vector
{
	int32		vl_len_;
	float4		vec_data[FLEXIBLE_ARRAY_MEMBER];	/* Data content is here */
} Vector;
 
Vector* new_vec(int len);
Vector *str_conv_vec(char* input);
char *vec_conv_str(Vector * vec);
 
Vector* new_vec(int len){
	// 		   vl_len_    dimension       vec_data
	int size = VARHDRSZ + sizeof(float4)*len;
	Vector *vec = (Vector *) palloc(size);
	SET_VARSIZE(vec, size);
	return vec;
}
int get_dimension(Vector * vec) {
	return (VARSIZE(vec)-VARHDRSZ)/sizeof(float);
}
// 字符串转向量: string'{a, b, c, d}' -> []float{a,b,c,d}
Vector *str_conv_vec(char* input) {
	// vector以'{'开头以及以'}'结尾
	int len = strlen(input), psb_sz = 0;
	if (strlen(input)<2 || input[0]!='{' || input[len-1]!='}') {
       ereport(ERROR,(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
            errmsg("invalid input syntax for type %s: \"%s\"", 
				"vector", input)));
    }
	// 根据','的数量，初步判断需要分配空间的大小
	for(int i=0;i<len;i++) {
		if (input[i]==',') psb_sz++;
	}
	// 创建临时buff
	float4 *buff = (float4 *)palloc0(sizeof(float4)*(psb_sz+1));
	// 解析字符串
	char *tmpStr = (char *)palloc0(strlen(input)+5);
	strcpy(tmpStr, input);
	char *p = tmpStr, *endptr;
    p[0]=0;
	int dimension = 0;
    while((p=strtok(p+strlen(p)+1,",}")) != NULL) {
		// 检查末尾是否有空格
		float4 data;
		char next_ch = p[strlen(p)+1];
		if(p[strlen(p)-1]==' ' ){
			ereport(ERROR,(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                errmsg("invalid input syntax for type %s: \"%s\"", 
					"vector-element", p)));
		}
		// 处理"{,,}" 和 "{,}"
		if (next_ch == ',' || next_ch == '}') {
			ereport(ERROR,(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                errmsg("invalid input syntax for type %s: \"%s\"", 
					"vector", input)));
		}
		data = strtof(p, &endptr);
		if (strlen(endptr)!=0) {  // endptr不为空, float识别出错
			ereport(ERROR,(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                errmsg("invalid input syntax for type %s: \"%s\"", 
					"vector-element", p)));
		}
		buff[dimension] = data;
        dimension++;
    }
	// vector维度为0
    if (dimension==0) {
		ereport(ERROR,(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
            errmsg("invalid input syntax for type %s: \"%s\"", 
				"vector", input)));
	}
	// 创建vector对象,并把buff拷贝到vec_data中
	Vector *vec = new_vec(dimension);
	memcpy(VARDATA(vec), buff, dimension*sizeof(float4));
	pfree(buff);
	return vec;
}
 
 
// 向量转字符串: []float{a,b,c,d} -> string'{a, b, c, d}'
char *vec_conv_str(Vector * vec) {
	int vec_dimension = get_dimension(vec);
	char *res = (char*) palloc0(30 * vec_dimension);
	res[0] = '{';
	for(int i=0; i<vec_dimension; i++){
		char *str_val = float_to_shortest_decimal(vec->vec_data[i]);
		strcat(res, str_val);
		if (i==vec_dimension-1) {
			strcat(res, (char*)"}");
		}else {
			strcat(res, (char*)",");
		}
		pfree(str_val);
	}
	return res;
}
 
/*****************************************************************************
 * Input/Output functions
 *****************************************************************************/
 
// input function
PG_FUNCTION_INFO_V1(vector_in);
 
Datum
vector_in(PG_FUNCTION_ARGS)
{
	char *str = PG_GETARG_CSTRING(0);
	Vector *vec = str_conv_vec(str);
	
	int vec_dimension = get_dimension(vec);
	PG_RETURN_POINTER(vec);
}
 
// output function
PG_FUNCTION_INFO_V1(vector_out);
 
Datum
vector_out(PG_FUNCTION_ARGS)
{
	Vector *vec = (Vector *) PG_GETARG_POINTER(0);
	char   *result = vec_conv_str(vec);
	PG_RETURN_CSTRING(result);
}
 
/*****************************************************************************
 * New Operators
 *****************************************************************************/
 
// 运算符 '<#>' 获取vec维度
PG_FUNCTION_INFO_V1(vector_dimension);
 
Datum
vector_dimension(PG_FUNCTION_ARGS)
{
	Vector    *a = (Vector *) PG_GETARG_POINTER(0);
	int vec_dimension = get_dimension(a);
	PG_RETURN_INT32(vec_dimension);
}
 
// 运算符 '<->' 获取两个vec间L2距离
PG_FUNCTION_INFO_V1(vector_distance);
 
Datum
vector_distance(PG_FUNCTION_ARGS)
{
	Vector    *a = (Vector *) PG_GETARG_POINTER(0);
	Vector    *b = (Vector *) PG_GETARG_POINTER(1);
	int32 len = get_dimension(a);
	float4 result = 0;
	if (len != get_dimension(b)) {
		ereport(ERROR,(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
            errmsg("vectors in different dimensions "
				"cannot calculate L2 distance.")));
	}
	for(int i=0; i<len; i++) {
		float4 tmp = a->vec_data[i] - b->vec_data[i];
		result += tmp * tmp;
	}
	result = (float4)sqrt(result);
	PG_RETURN_FLOAT4(result);
}
 
// 运算符 '+'  向量加法
PG_FUNCTION_INFO_V1(vector_add);
 
Datum
vector_add(PG_FUNCTION_ARGS)
{
	Vector    *a = (Vector *) PG_GETARG_POINTER(0);
	Vector    *b = (Vector *) PG_GETARG_POINTER(1);
	int32 	  len = get_dimension(a);
	Vector 	  *result;
	// 判断是否维度是否相等
	if (len != get_dimension(b)) {
		ereport(ERROR,(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
        	errmsg("vectors of different dimensions cannot be added together.")));
	}
	result = new_vec(len);
	for(int i=0; i<len; i++) {
		result->vec_data[i] = a->vec_data[i] + b->vec_data[i];
	}
	PG_RETURN_POINTER(result);
}
 
// 运算符 '-' 向量减法
PG_FUNCTION_INFO_V1(vector_sub);
 
Datum
vector_sub(PG_FUNCTION_ARGS)
{
	Vector    *a = (Vector *) PG_GETARG_POINTER(0);
	Vector    *b = (Vector *) PG_GETARG_POINTER(1);
	int32     len = get_dimension(a);
	Vector 	  *result;
	if (len != get_dimension(b)) {
		ereport(ERROR,(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
            errmsg("vectors in different dimensions cannot be subtracted.")));
	}
	// 判断是否维度是否相等
	result = new_vec(len);
	for(int i=0; i<len; i++) {
		result->vec_data[i] = a->vec_data[i] - b->vec_data[i];
	}
	PG_RETURN_POINTER(result);
}