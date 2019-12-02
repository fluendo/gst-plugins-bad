#include <sal.h>

#define __in _In_
#define __out _Out_
#define __deref_out
#define __deref_in
#define __deref_inout_opt
#define AM_NOVTABLE
#define __in_ecount_opt(a)
#define __inout_ecount_full(a)
#define __in_bcount_opt(a)
#define __out_ecount_part(a,b)
#define __deref_out_op
#define __field_ecount_opt(a)
#define __range(a,b)
#define __deref_out_opt
#define __control_entrypoint(a)
#define __deref_out_range(a,b)


// DIBSIZE calculates the number of bytes required by an image

#define WIDTHBYTES(bits) ((DWORD)(((bits)+31) & (~31)) / 8)
#define DIBWIDTHBYTES(bi) (DWORD)WIDTHBYTES((DWORD)(bi).biWidth * (DWORD)(bi).biBitCount)
#define _DIBSIZE(bi) (DIBWIDTHBYTES(bi) * (DWORD)(bi).biHeight)
#define DIBSIZE(bi) ((bi).biHeight < 0 ? (-1)*(_DIBSIZE(bi)) : _DIBSIZE(bi))