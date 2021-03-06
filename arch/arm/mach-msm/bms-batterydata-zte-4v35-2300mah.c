#include <linux/batterydata-lib.h>

static struct single_row_lut fcc_temp = {
	.x		= {-20, 0, 25, 40, 60},
	.y		= {2332, 2334, 2349, 2342, 2335},
	.cols	= 5
};

static struct single_row_lut fcc_sf = {
	.x		= {0},
	.y		= {100},
	.cols	= 1
};

static struct sf_lut rbatt_sf = {
	.rows		= 29,
	.cols		= 5,
	.row_entries		= {-20, 0, 25, 40, 60},
	.percent	= {100, 95, 90, 85, 80, 75, 70, 65, 60, 55, 50, 45, 40, 35, 30, 25, 20, 15, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0},
	.sf		= {
				{684, 244, 100, 71, 65},
				{683, 244, 100, 71, 65},
				{652, 247, 104, 74, 67},
				{627, 244, 107, 77, 69},
				{619, 242, 111, 79, 71},
				{610, 250, 115, 83, 73},
				{588, 237, 113, 85, 75},
				{597, 242, 111, 88, 78},
				{606, 246, 103, 85, 78},
				{614, 243, 102, 74, 68},
				{628, 243, 102, 74, 68},
				{649, 244, 105, 76, 70},
				{679, 244, 107, 78, 73},
				{726, 246, 109, 81, 76},
				{801, 256, 110, 82, 74},
				{893, 270, 111, 80, 70},
				{1009, 290, 107, 78, 70},
				{1167, 320, 102, 76, 70},
				{660, 263, 104, 77, 70},
				{689, 265, 106, 78, 72},
				{726, 269, 109, 80, 75},
				{771, 272, 112, 83, 77},
				{823, 275, 112, 84, 75},
				{894, 276, 108, 79, 71},
				{993, 280, 106, 77, 71},
				{1211, 288, 108, 79, 73},
				{2403, 301, 117, 84, 77},
				{10946, 404, 934, 97, 122},
				{60347, 42812, 35216, 41540, 36756}
	}
};
#if 0
static struct sf_lut r1batt = {
	.rows		= 29,
	.cols		= 5,
	.row_entries		= {-20, 0, 25, 40, 60},
	.percent	= {100, 95, 90, 85, 80, 75, 70, 65, 60, 55, 50, 45, 40, 35, 30, 25, 20, 15, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0},
	.sf		= {
				{361, 228, 131, 106, 102},
				{361, 228, 131, 106, 102},
				{360, 226, 131, 106, 103},
				{357, 225, 130, 106, 103},
				{358, 224, 130, 106, 103},
				{358, 223, 130, 107, 103},
				{360, 226, 132, 107, 103},
				{359, 227, 133, 107, 104},
				{359, 229, 132, 107, 104},
				{362, 227, 133, 108, 104},
				{365, 226, 133, 108, 104},
				{362, 228, 134, 108, 104},
				{363, 228, 134, 108, 105},
				{372, 227, 135, 109, 105},
				{377, 227, 135, 109, 105},
				{380, 226, 135, 109, 105},
				{388, 227, 135, 109, 106},
				{395, 226, 135, 110, 106},
				{365, 235, 137, 110, 106},
				{371, 236, 137, 110, 106},
				{370, 233, 137, 110, 106},
				{374, 233, 137, 110, 106},
				{377, 234, 137, 110, 106},
				{382, 234, 138, 111, 107},
				{390, 236, 138, 111, 107},
				{401, 238, 139, 112, 108},
				{418, 236, 140, 113, 109},
				{435, 241, 144, 115, 113},
				{331, 145, 149, 74, 89}
	}
};
#endif

#if 0
static struct sf_lut r2batt = {
	.rows		= 29,
	.cols		= 5,
	.row_entries		= {-20, 0, 25, 40, 60},
	.percent	= {100, 95, 90, 85, 80, 75, 70, 65, 60, 55, 50, 45, 40, 35, 30, 25, 20, 15, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0},
	.sf		= {
				{925, 232, 57, 29, 21},
				{924, 232, 57, 29, 21},
				{866, 239, 66, 34, 24},
				{823, 235, 73, 39, 28},
				{806, 231, 79, 44, 31},
				{790, 247, 88, 50, 35},
				{746, 221, 81, 53, 39},
				{764, 228, 77, 59, 44},
				{782, 234, 62, 54, 44},
				{793, 231, 59, 32, 24},
				{816, 232, 60, 32, 24},
				{859, 231, 64, 36, 29},
				{914, 231, 68, 39, 34},
				{993, 236, 71, 44, 38},
				{1129, 255, 73, 46, 35},
				{1299, 283, 74, 43, 28},
				{1510, 320, 67, 38, 27},
				{1800, 376, 57, 34, 26},
				{877, 261, 60, 35, 27},
				{926, 264, 64, 38, 31},
				{995, 274, 69, 42, 35},
				{1077, 280, 74, 47, 39},
				{1171, 283, 74, 48, 36},
				{1300, 286, 66, 38, 28},
				{1478, 291, 62, 35, 27},
				{1877, 304, 65, 37, 31},
				{4101, 330, 80, 45, 37},
				{20144, 520, 1612, 68, 118},
				{113122, 80342, 66058, 78023, 69014}
	}
};
#endif

static struct pc_temp_ocv_lut pc_temp_ocv = {
	.rows		= 29,
	.cols		= 5,
	.temp		= {-20, 0, 25, 40, 60},
	.percent	= {100, 95, 90, 85, 80, 75, 70, 65, 60, 55, 50, 45, 40, 35, 30, 25, 20, 15, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0},
	.ocv		= {
				{4324, 4322, 4317, 4316, 4309},
				{4237, 4250, 4251, 4252, 4248},
				{4173, 4192, 4197, 4199, 4196},
				{4116, 4136, 4146, 4148, 4145},
				{4070, 4082, 4098, 4099, 4096},
				{4013, 4042, 4058, 4054, 4050},
				{3931, 3959, 3991, 4003, 4007},
				{3892, 3921, 3951, 3969, 3968},
				{3860, 3896, 3908, 3928, 3928},
				{3830, 3865, 3873, 3876, 3876},
				{3804, 3837, 3844, 3847, 3847},
				{3782, 3810, 3821, 3823, 3824},
				{3764, 3785, 3802, 3803, 3804},
				{3748, 3763, 3785, 3787, 3787},
				{3733, 3748, 3770, 3772, 3767},
				{3717, 3736, 3755, 3754, 3742},
				{3699, 3725, 3730, 3730, 3717},
				{3674, 3713, 3702, 3700, 3687},
				{3637, 3698, 3689, 3681, 3669},
				{3626, 3692, 3687, 3680, 3668},
				{3613, 3684, 3685, 3679, 3667},
				{3597, 3674, 3680, 3676, 3663},
				{3576, 3658, 3667, 3668, 3651},
				{3551, 3633, 3635, 3639, 3618},
				{3515, 3597, 3585, 3590, 3568},
				{3465, 3547, 3516, 3525, 3502},
				{3393, 3476, 3417, 3434, 3408},
				{3298, 3363, 3262, 3283, 3248},
				{3000, 3000, 3000, 3000, 3000}
	}
};

static struct sf_lut pc_sf = {
	.rows		= 1,
	.cols		= 1,
	.row_entries		= {0},
	.percent	= {100},
	.sf			= {
				{100}
	}
};

struct bms_battery_data zte_4v35_2300mah_data = {
	.fcc				= 2300,
	.fcc_temp_lut			= &fcc_temp,
	.fcc_sf_lut				= &fcc_sf,
	.pc_temp_ocv_lut		= &pc_temp_ocv,
	.pc_sf_lut				= &pc_sf,
	.rbatt_sf_lut			= &rbatt_sf,
	.default_rbatt_mohm	= 188
};
