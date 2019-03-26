/*
 * decn.c
 *
 *  Created on: Mar 21, 2019
 *      Author: jeffrey
 */

#include "../utils.h"

//#define DEBUG
//#define DEBUG_ADD
//#define DEBUG_COMPARE_MAGN

#ifndef DESKTOP
#undef DEBUG
#undef DEBUG_ADD
#undef DEBUG_COMPARE_MAGN
#endif

#ifdef DESKTOP
#include <stdio.h>
#endif

#ifdef DESKTOP
#include <assert.h>
#else
#define assert(x)
#endif

#include "decn.h"



void copy_decn(dec80* dest, const dec80* src){
	uint8_t i;
	dest->exponent = src->exponent;

	//copy nibbles
	for (i = 0; i < DEC80_NUM_LSU; i++){
		dest->lsu[i] = src->lsu[i];
	}
}

int16_t get_exponent(const dec80* x){
	int16_t exponent = x->exponent;
	if (exponent & 0x4000){ //negative
		return exponent | 0x8000;
	} else { //positive
		return exponent & 0x7fff;
	}
}


static void zero_remaining_dec80(dec80* dest, uint8_t digit100){
	for ( ; digit100 < DEC80_NUM_LSU; digit100++){
		dest->lsu[digit100] = 0;
	}
}

static void shift_right(dec80* x){
	uint8_t high = 0, low = 0, old_low = 0;
	uint8_t i;
	for (i = 0; i < DEC80_NUM_LSU; i++){
		high = x->lsu[i] / 10;
		low = x->lsu[i] % 10;
		x->lsu[i] = high + (old_low*10);
		old_low = low;
	}
}

static void shift_left(dec80* x){
	uint8_t high = 0, low = 0, old_high = 0;
	int8_t i;
	for (i = DEC80_NUM_LSU - 1; i >= 0 ; i--){
		high = x->lsu[i] / 10;
		low = x->lsu[i] % 10;
		x->lsu[i] = old_high + (low*10);
		old_high = high;
	}
}

static void remove_leading_zeros(dec80* x){
	uint8_t digit100;
	uint8_t is_negative = (x->exponent < 0 ? 1 : 0);
	uint8_t exponent = get_exponent(x);

	//find first non-zero digit100
	for (digit100 = 0; digit100 < DEC80_NUM_LSU; digit100++){
		if (x->lsu[digit100] != 0){
			break;
		}
	}
	exponent -= digit100 * 2; //base 100

	if (digit100 != 0 && digit100 < DEC80_NUM_LSU){ //found non-zero digit100
		uint8_t i;
		//copy digit100s
		for (i = 0; digit100 < DEC80_NUM_LSU; i++, digit100++){
			x->lsu[i] = x->lsu[digit100];
		}
		//zero out remaining digit100s, now that number left-aligned
		zero_remaining_dec80(x, i);
		//ensure MSdigit in MSdigit100 is > 0
		if (x->lsu[digit100] < 10) {
			shift_left(x);
			exponent--;
		}
		//write back exponent
		if (is_negative){
			x->exponent = exponent | 0x8000;
		} else {
			x->exponent = exponent & 0x7fff;
		}
	}
}

void build_dec80(dec80* dest, const char* signif_str, int16_t exponent){
	enum {
		SIGN_ZERO,
		SIGN_ZERO_SEEN_POINT,
		SIGN_NEG_ZERO,
		SIGN_NEG_ZERO_SEEN_POINT,
		SIGN_POS,
		SIGN_POS_SEEN_POINT,
		SIGN_NEG,
		SIGN_NEG_SEEN_POINT
	};
#ifdef DESKTOP
	static_assert(SIGN_ZERO < SIGN_NEG_ZERO, "do not change enum values");
#endif
#define SEEN_POINT(curr_sign) ((curr_sign) & 1)
#define IS_ZERO(curr_sign) ((curr_sign) <= SIGN_NEG_ZERO_SEEN_POINT)
#define IS_NEG(curr_sign) ((curr_sign) >= SIGN_NEG)

	uint8_t i = 0;
	uint8_t nibble_i = 0;
	uint8_t save_nibble = 0;
	int8_t num_lr_points = 0; //number of digits to the right (-) or left of decimal point
	int8_t curr_sign = SIGN_ZERO;

	//check first digit
	if (signif_str[0] == '\0'){
		set_dec80_zero(dest);
		return;
	} else if (signif_str[0] == '-'){
		curr_sign = SIGN_NEG_ZERO;
	}

	//go through digits
	while (1){
		if (signif_str[i] == '.'){
			if (!SEEN_POINT(curr_sign)){
				//begin tracking number of digits to right of decimal point
				curr_sign |= 1; //seen point
			} else {
				//multiple '.'s in string
#ifdef DEBUG
				printf("  ERROR: multiple '.'s in string\n");
#endif
				set_dec80_NaN(dest);
				return;
			}
		} else if (signif_str[i] >= '1' && signif_str[i] <= '9'){
			if (nibble_i < DEC80_NUM_LSU*2){
				if (nibble_i & 1) { //odd
					dest->lsu[nibble_i/2] = save_nibble * 10 + (signif_str[i] - '0');
				} else {
					save_nibble = signif_str[i] - '0';
				}
			}
			nibble_i++;
			//track sign
			if (curr_sign == SIGN_ZERO){
				curr_sign = SIGN_POS;
			} else if (curr_sign == SIGN_ZERO_SEEN_POINT){
				curr_sign = SIGN_POS_SEEN_POINT;
			} else if (curr_sign == SIGN_NEG_ZERO){
				curr_sign = SIGN_NEG;
			} else if (curr_sign == SIGN_NEG_ZERO_SEEN_POINT){
				curr_sign = SIGN_NEG_SEEN_POINT;
			}
			//track number digits L/R of decimal point
			if (!SEEN_POINT(curr_sign)){ //haven't seen decimal point yet
				num_lr_points++; //increase left count
			}
		} else if (signif_str[i] == '0'){
			//make sure not a leading zero
			if (!IS_ZERO(curr_sign)){ //non-zero value
				if (nibble_i < DEC80_NUM_LSU*2){
					if (nibble_i & 1) { //odd
						dest->lsu[nibble_i/2] = save_nibble * 10 + 0;
					} else {
						save_nibble = 0;
					}
				}
				nibble_i++;
			}
			//track number digits L/R of decimal point
			if (SEEN_POINT(curr_sign)){
				if (IS_ZERO(curr_sign)){ //tracking 0s to right of point
					if (num_lr_points <= 0){ //no left count exists
						num_lr_points--; //increase right count
					}
				}
			} else { //haven't seen decimal point yet
				if (!IS_ZERO(curr_sign)){ //not a leading zero
					num_lr_points++; //increase left count
				}
			}
		} else if (signif_str[i] == '\0'){ //done
			if (IS_ZERO(curr_sign)){
				//zero
				set_dec80_zero(dest);
				return;
			} else {
				//not zero
				int8_t new_exponent;
				//write out saved nibble, if it exists
				// (saved while nibble_i even, nibble_i then incremented to odd)
				if (nibble_i & 1){ //odd
					dest->lsu[nibble_i/2] = save_nibble * 10;
					nibble_i++; //increment for zeroing out
				}
				//zero out any old data
				zero_remaining_dec80(dest, nibble_i/2);
				// adjust exponent for left-aligned significand input
				// or for number of digits past decimal point
				if (num_lr_points > 0){ //left count exists
					assert(DEC80_NUM_LSU*2 > num_lr_points);
					new_exponent = exponent + (num_lr_points - 1); //1 digit left of implicit point
					//check for overflow
					if (new_exponent < exponent || exponent > DEC80_MAX_EXP){
	#ifdef DEBUG
						printf("   overflow (new_exp, exp)=(%d,%d)\n",
						         new_exponent, exponent);
	#endif
						set_dec80_NaN(dest);
						return;
					}
				} else if (num_lr_points < 0) { //right count exists
					// (-num_past_point represents #0s right of decimal)
					// (this ends up being a subtraction)
					new_exponent = exponent + num_lr_points;
					new_exponent -= 1; //decimal point after 1st non-zero number
					//check for underflow
					if (new_exponent > exponent || exponent < DEC80_MIN_EXP){
	#ifdef DEBUG
						printf("   underflow (new_exp, exp)=(%d,%d)\n",
						         new_exponent, exponent);
	#endif
						set_dec80_NaN(dest);
						return;
					}
				} else {
					//no change
					new_exponent = exponent;
				}
				exponent = new_exponent;
				//set negative bit
				if (IS_NEG(curr_sign)){
					exponent |= 0x8000;
				} else {
					exponent &= 0x7fff;
				}
				dest->exponent = exponent;
#ifdef DEBUG
				printf("   num_lr_points (%d), new_exp (%d), sign (%d), exp (%d)\n",
				        num_lr_points, new_exponent, curr_sign, exponent);
				printf("   ");
				for (i = 0; i < DEC80_NUM_LSU; i++){
					printf("%02d,", dest->lsu[i]);
				}
				printf("\n");
#endif

				return;
			}
		}
		i++;
		assert(i < DECN_BUF_SIZE);
	} // while 1
#undef SEEN_POINT
#undef IS_ZERO
#undef IS_NEG
}



void set_dec80_zero(dec80* dest){
	uint8_t i;

	//clear exponent
	dest->exponent = 0;
	//clear nibbles
	for (i = 0; i < DEC80_NUM_LSU; i++){
		dest->lsu[i] = 0;
	}
}

static uint8_t decn_is_zero(const dec80* x){
	uint8_t i;
	for (i = 0; i < DEC80_NUM_LSU; i++){
		if (x->lsu[i] != 0){
			return 0;
		}
	}

	return 1;
}

void set_dec80_NaN(dec80* dest){
	uint8_t i;

	//set exponent to special val
	dest->exponent = 0xff;
	//clear nibbles
	for (i = 0; i < DEC80_NUM_LSU; i++){
		dest->lsu[i] = 0;
	}
}

void negate_decn(dec80* x){
	static const int16_t xor_val = -(0x7fff) - 1;
	(x->exponent) ^= xor_val;
}

int8_t compare_magn(const dec80* a, const dec80* b){ //a<b: -1, a==b: 0, a>b: 1
	uint8_t a_i, b_i;
	int16_t a_exp=0, b_exp=0;
	int8_t a_signif_b = 0; //a<b: -1, a==b: 0, a>b: 1
	dec80 a_tmp, b_tmp;
	//copy
	copy_decn(&a_tmp, a);
	copy_decn(&b_tmp, b);
	//normalize
	remove_leading_zeros(&a_tmp);
	remove_leading_zeros(&b_tmp);
	//compare signifcands while tracking magnitude
	for (
		a_i = 0, b_i = 0;
		a_i < DEC80_NUM_LSU && b_i < DEC80_NUM_LSU;
		a_i++, b_i++, a_exp+=2, b_exp+=2
		)
	{
		//set signif. inequality if this is first digit that is different
		if (a_signif_b == 0 && (a_tmp.lsu[a_i] < b_tmp.lsu[b_i])){
#ifdef DEBUG_COMPARE_MAGN
			printf("a_signif_b -1: a.lsu[%d]=%d, b.lsu[%d]=%d\n",
			        a_i, a_tmp.lsu[a_i], b_i, b_tmp.lsu[b_i]);
#endif
			a_signif_b = -1;
		} else if (a_signif_b == 0 && (a_tmp.lsu[a_i] > b_tmp.lsu[b_i])){
#ifdef DEBUG_COMPARE_MAGN
			printf("a_signif_b  1: a.lsu[%d]=%d, b.lsu[%d]=%d\n",
			        a_i, a_tmp.lsu[a_i], b_i, b_tmp.lsu[b_i]);
#endif
			a_signif_b = 1;
		}
	}
	//calculate exponents
	a_exp += get_exponent(&a_tmp);
	b_exp += get_exponent(&b_tmp);
	//compare exponents
	if (a_exp > b_exp){
#ifdef DEBUG
		printf("a_exp > b_exp\n");
#endif
		return 1;
	} else if (a_exp < b_exp){
#ifdef DEBUG
		printf("a_exp < b_exp\n");
#endif
		return -1;
	}
	//exponents equal, compare by significand
#ifdef DEBUG
		printf("a_signif_b (%d)\n", a_signif_b);
#endif
	return a_signif_b;
}

int8_t compare_decn(const dec80* a, const dec80* b){ //a<b: -1, a==b: 0, a>b: 1
	int8_t is_neg;

	//handle zero special cases
	if (decn_is_zero(a) && decn_is_zero(b)){
		return 0;
	} else if (decn_is_zero(a)){
		if (b->exponent < 0){
			return 1;
		} else {
			return -1;
		}
	} else if (decn_is_zero(b)){
		if (a->exponent < 0){
			return -1;
		} else {
			return 1;
		}
	}
	//handle cases where signs differ
	if (a->exponent < 0 && b->exponent > 0){
		return -1;
	} else if (a->exponent > 0 && b->exponent < 0){
		return 1;
	}
	//signs must now be the same, either both pos, or both neg
	is_neg = (a->exponent < 0 ? -1 : 1);

	return is_neg * compare_magn(a, b);
}

//WARNING: for add_decn() and sub_mag() functions only
//rescales acc up to exponent (increase exponent of acc, while shifting right)
//the actual value of acc->exponent remains unchanged
//both acc, and the number from which exponent was taking must be stripped of leading 0s first
static void _incr_exp(dec80* acc, int16_t exponent){
	int16_t curr_exp = get_exponent(acc);
#ifdef DEBUG_ADD
	uint8_t is_neg = (acc->exponent < 0 ? 1 : 0);
	printf("   (is_neg,curr_exp,exponent)=(%d,%d,%d)\n",
	        is_neg, curr_exp, exponent);
#endif
	assert(exponent > curr_exp);
	while (curr_exp != exponent){
		//shift right
		shift_right(acc);
		curr_exp++;
	}

	//curr_exp does NOT get written back to acc->exponent
}

//for internal use only,
//acc must be larger than x in absolute value
//subtract by equal addition algorithm
static void sub_mag(dec80* acc, const dec80* x){
	int8_t i;
	uint8_t carry = 0;
	dec80 tmp;
	copy_decn(&tmp, x);
	//normalize
	remove_leading_zeros(acc);
	remove_leading_zeros(&tmp);
	_incr_exp(&tmp, get_exponent(acc));
#ifdef DEBUG_ADD
	extern char buf[DECN_BUF_SIZE];
	dec80_to_str(buf, &tmp);
	printf("incr_exp tmp: %s\n", buf);
#endif
	//do subtraction
	for (i = DEC80_NUM_LSU - 1; i >=0; i--){
		uint8_t digit100;
		if (acc->lsu[i] >= (tmp.lsu[i] + carry)){
			digit100 = acc->lsu[i] - (tmp.lsu[i] + carry);
			carry = 0;
		} else {
			digit100 = acc->lsu[i] + 100 - (tmp.lsu[i] + carry);
			carry = 1;
		}
		assert(digit100 < 100);
		acc->lsu[i] = digit100;
	}
	assert(carry == 0); //shouldn't be carry out if |acc| > |x|
}

void add_decn(dec80* acc, const dec80* x){
	dec80 tmp;
	int8_t rel;
	uint8_t carry = 0;
	int8_t i;

	//check if zero
	if (decn_is_zero(x)){
		return;
	} else if (decn_is_zero(acc)){
		copy_decn(acc, x);
		return;
	}
	//handle cases where signs differ
	if (acc->exponent < 0 && x->exponent > 0){
		// -acc, +x
		rel = compare_magn(acc, x);
		if (rel == 1){
#ifdef DEBUG_ADD
			printf("|-acc| > |+x|\n");
#endif
			sub_mag(acc, x);
			return;
		} else if (rel == -1){
#ifdef DEBUG_ADD
			printf("|-acc| < |+x|\n");
#endif
			copy_decn(&tmp, x);
			sub_mag(&tmp, acc);
			copy_decn(acc, &tmp);
			return;
		} else { //equal
#ifdef DEBUG_ADD
			printf("|-acc| == |+x|\n");
#endif
			set_dec80_zero(acc);
			return;
		}
	} else if (acc->exponent > 0 && x->exponent < 0){
		// +acc, -x
		rel = compare_magn(acc, x);
		if (rel == 1){
#ifdef DEBUG_ADD
			printf("|+acc| > |-x|\n");
#endif
			sub_mag(acc, x);
			return;
		} else if (rel == -1){
#ifdef DEBUG_ADD
			printf("|+acc| < |-x|\n");
#endif
			copy_decn(&tmp, x);
			sub_mag(&tmp, acc);
			negate_decn(&tmp);
			copy_decn(acc, &tmp);
			return;
		} else { //equal
#ifdef DEBUG_ADD
			printf("|+acc| == |-x|\n");
#endif
			set_dec80_zero(acc);
			return;
		}
	}
	//signs must now be the same, begin adding
	copy_decn(&tmp, x);
	//normalize
	remove_leading_zeros(acc);
	remove_leading_zeros(&tmp);
#ifdef DEBUG_ADD
	extern char buf[DECN_BUF_SIZE];
	dec80_to_str(buf, acc);
	printf("rem_leading_zeros acc: %s\n", buf);
	dec80_to_str(buf, &tmp);
	printf("rem_leading_zeros tmp: %s\n", buf);
#endif
	rel = compare_magn(acc, &tmp);
	if (rel == 1){
		_incr_exp(&tmp, get_exponent(acc));
	} else if (rel == -1){
		_incr_exp(acc, get_exponent(&tmp));
	}
#ifdef DEBUG_ADD
	extern char buf[DECN_BUF_SIZE];
	dec80_to_str(buf, acc);
	printf("incr_exp acc: %s\n", buf);
	dec80_to_str(buf, &tmp);
	printf("incr_exp tmp: %s\n", buf);
#endif
	//do addition
	for (i = DEC80_NUM_LSU - 1; i >= 0; i--){
		uint8_t digit100 = acc->lsu[i] + tmp.lsu[i] + carry;
		acc->lsu[i] = digit100 % 100;
		carry = digit100 / 100;
		assert(carry < 100);
	}
	//may need to rescale number
	if (carry > 0){
		int16_t curr_exp = get_exponent(acc);
		rel = (acc->exponent < 0 ? 1 : 0); //is_neg?
#ifdef DEBUG_ADD
		printf("carry out: %d", carry);
#endif
		//shift right
		if (carry < 10){
			shift_right(acc);
			acc->lsu[0] += carry*10; //carry gets shifted into most significant digit
			curr_exp++;
		} else {
			shift_right(acc);
			shift_right(acc);
			acc->lsu[0] = carry;
			curr_exp+=2;
		}
		//track sign
		if (rel){ //is_neg
			acc->exponent = curr_exp | 0x8000;
		} else {
			acc->exponent = curr_exp | 0x7fff;
		}
	}
}

//buf should hold at least 18 + 4 + 5 + 1 = 28
void dec80_to_str(char* buf, const dec80* x){
	uint8_t i = 0;
	uint8_t digit100;
	int16_t exponent = 0;
	uint8_t trailing_zeros = 0;

	//check sign of number
	if (x->exponent < 0){
#ifdef DEBUG
		printf ("  negative  ");
#endif
		buf[i] = '-';
		i++;
	}
	//discard leading zeros
	for (digit100 = 0; digit100 < DEC80_NUM_LSU; digit100++){
		if (x->lsu[digit100] != 0){
			break;
		}
		exponent -= 2; //base 100
	}
#ifdef DEBUG
	printf ("  leading 0s discarded (%d)  ", digit100);
#endif
	//print 1st nonzero

	//handle corner case of 0
	if (digit100 == DEC80_NUM_LSU){
#ifdef DEBUG
		printf ("  corner case, set to 0  ");
#endif
		buf[0] = '0';
		buf[1] = '\0';
		return;
	}

	//print 1st two digits
	if (x->lsu[digit100] >= 10){ //2 digits
		buf[i] = (x->lsu[digit100] / 10) + '0';
		i++;
		buf[i] = '.';
		i++;
		buf[i] = (x->lsu[digit100] % 10) + '0';
		i++;
	} else { //1 digit
		exponent--;
		buf[i] = x->lsu[digit100] + '0';
		i++;
		buf[i] = '.';
		i++;
	}
	digit100++;
	//print rest of significand
	for ( ; digit100 < DEC80_NUM_LSU; digit100++){
		if (x->lsu[digit100] > 10){ //2 digits
			buf[i] = (x->lsu[digit100] / 10) + '0';
			i++;
			buf[i] = (x->lsu[digit100] % 10) + '0';
			i++;
			//track trailing 0s
			if ((x->lsu[digit100] % 10) == 0){
				trailing_zeros++;
			} else {
				trailing_zeros = 0;
			}
		} else { //1 digit
			buf[i] = '0';
			i++;
			buf[i] = x->lsu[digit100] + '0';
			i++;
			//track trailing 0s
			if ((x->lsu[digit100]) == 0){
				trailing_zeros += 2;
			} else {
				trailing_zeros = 0;
			}
		}
	}
	//calculate exponent
#ifdef DEBUG
	printf ("  exponent (%d,", exponent);
#endif
	exponent += get_exponent(x); //base 100
#ifdef DEBUG
	printf ("%d)", exponent);
#endif
	//remove trailing zeros
	i -= trailing_zeros;

	if (exponent != 0){
		buf[i] = 'E';
		i++;
		if (exponent < 0){
			buf[i] = '-';
			i++;
			u32str(-exponent, &buf[i], 10); //adds null terminator automatically
		} else {
			u32str(exponent, &buf[i], 10); //adds null terminator automatically
		}
	} else {
		//null terminate
		buf[i] = '\0';
	}

#ifdef DEBUG
	printf ("  final i (%d)  ", i);
#endif
}


