#ifndef FIXED_POINT_H
#define FIXED_POINT_H

#include <stdint.h>

#define SCALING_FACTOR                                              4096
#define LOG_BASE2_OF_SCALING_FACTOR                                 12
#define SHIFT_VALUE_REQUIRED_TO_MULTIPLY_OR_DIVIDE_SCALING_FACTOR   LOG_BASE2_OF_SCALING_FACTOR

#define GET_FIXED_POINT_OF_NUM(x)                             ( x << SHIFT_VALUE_REQUIRED_TO_MULTIPLY_OR_DIVIDE_SCALING_FACTOR )

#define GET_POSITIVE_INTEGER_FROM_FIXED_POINT(x)                    ( ( x + ((SCALING_FACTOR)>>1) ) >> SHIFT_VALUE_REQUIRED_TO_MULTIPLY_OR_DIVIDE_SCALING_FACTOR )

#define GET_NEGATIVE_INTEGER_FROM_FIXED_POINT(x)                    ( ( x - ((SCALING_FACTOR)>>1) ) >> SHIFT_VALUE_REQUIRED_TO_MULTIPLY_OR_DIVIDE_SCALING_FACTOR )

#define ADD_INT_TO_FIXED_POINT_VALUE(x, n)                          x + ( n << SHIFT_VALUE_REQUIRED_TO_MULTIPLY_OR_DIVIDE_SCALING_FACTOR )
                                                    
#define SUBTRACT_INT_FROM_FIXED_POINT_VALUE(x, n)                     x - ( n << SHIFT_VALUE_REQUIRED_TO_MULTIPLY_OR_DIVIDE_SCALING_FACTOR )

#define MULTIPLY_FIXED_POINT_VALUES(x, y)                           ( ( (int64_t)x ) * y ) >> SHIFT_VALUE_REQUIRED_TO_MULTIPLY_OR_DIVIDE_SCALING_FACTOR

#define DIVIDE_FIXED_POINT_VALUES(x, y)                             ( ( (int64_t)x )  << SHIFT_VALUE_REQUIRED_TO_MULTIPLY_OR_DIVIDE_SCALING_FACTOR ) / y



#endif