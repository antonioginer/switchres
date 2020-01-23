/**************************************************************

   modeline.cpp - Modeline generation and scoring routines

   ---------------------------------------------------------

   SwitchRes   Modeline generation engine for emulation

   License     GPL-2.0+
   Copyright   2010-2019 - Chris Kennedy, Antonio Giner

 **************************************************************/

#include <stdio.h>
#include <string.h>
#include "modeline.h"
#include "log.h"

#define max(a,b)({ __typeof__ (a) _a = (a);__typeof__ (b) _b = (b);_a > _b ? _a : _b; })
#define min(a,b)({ __typeof__ (a) _a = (a);__typeof__ (b) _b = (b);_a < _b ? _a : _b; })


//============================================================
//  PROTOTYPES
//============================================================

int get_line_params(modeline *mode, monitor_range *range);
int scale_into_range (int value, int lower_limit, int higher_limit);
int scale_into_range (float value, float lower_limit, float higher_limit);
int scale_into_aspect (int source_res, int tot_res, float original_monitor_aspect, float users_monitor_aspect, float *best_diff);
int stretch_into_range(float vfreq, monitor_range *range, bool interlace_allowed, float *interlace);
int total_lines_for_yres(int yres, float vfreq, monitor_range *range, float interlace);
float max_vfreq_for_yres (int yres, monitor_range *range, float interlace);

//============================================================
//  modeline_create
//============================================================

int modeline_create(modeline *s_mode, modeline *t_mode, monitor_range *range, generator_settings *cs)
{
	float vfreq = 0;
	float vfreq_real = 0;
	int xres = 0;
	int yres = 0;
	float interlace = 1;
	float doublescan = 1;
	float scan_factor = 1;
	int x_scale = 0;
	int y_scale = 0;
	int v_scale = 0;
	float x_diff = 0;
	float y_diff = 0;
	float v_diff = 0;
	float y_ratio = 0;
	float x_ratio = 0;

	// init all editable fields with source or user values
	if (t_mode->type & X_RES_EDITABLE)
		xres = cs->width? cs->width : s_mode->hactive;
	else
		xres = t_mode->hactive;
	if (t_mode->type & Y_RES_EDITABLE)
		yres = cs->height? cs->height : s_mode->vactive;
	else
		yres = t_mode->vactive;
	if (t_mode->type & V_FREQ_EDITABLE)
		vfreq = s_mode->vfreq;
	else
		vfreq = t_mode->vfreq;

	// lock resolution fields if required
	if (cs->width) t_mode->type &= ~X_RES_EDITABLE;
	if (cs->height) t_mode->type &= ~Y_RES_EDITABLE;

	// ··· Vertical refresh ···
	// try to fit vertical frequency into current range
	v_scale = scale_into_range(vfreq, range->vfreq_min, range->vfreq_max);

	if (!v_scale && (t_mode->type & V_FREQ_EDITABLE))
	{
		vfreq = vfreq < range->vfreq_min? range->vfreq_min : range->vfreq_max;
		v_scale = 1;
	}
	else if (v_scale != 1 && !(t_mode->type & V_FREQ_EDITABLE))
	{
		t_mode->result.weight |= R_OUT_OF_RANGE;
		return -1;
	}

	// ··· Vertical resolution ···
	// try to fit active lines in the progressive range first
	if (range->progressive_lines_min && (!t_mode->interlace || (t_mode->type & V_FREQ_EDITABLE)))
		y_scale = scale_into_range(yres, range->progressive_lines_min, range->progressive_lines_max);

	// if not possible, try to fit in the interlaced range, if any
	if (!y_scale && range->interlaced_lines_min && cs->interlace && (t_mode->interlace || (t_mode->type & V_FREQ_EDITABLE)))
	{
		y_scale = scale_into_range(yres, range->interlaced_lines_min, range->interlaced_lines_max);
		interlace = 2;
	}

	// if we succeeded, let's see if we can apply integer scaling
	if (y_scale == 1 || (y_scale > 1 && (t_mode->type & Y_RES_EDITABLE)))
	{
		// check if we should apply doublescan
		if (cs->doublescan && y_scale % 2 == 0)
		{
			y_scale /= 2;
			doublescan = 0.5;
		}
		scan_factor = interlace * doublescan;

		// calculate expected achievable refresh for this height
		vfreq_real = min(vfreq * v_scale, max_vfreq_for_yres(yres * y_scale, range, scan_factor));
		if (vfreq_real != vfreq * v_scale && !(t_mode->type & V_FREQ_EDITABLE))
		{
			t_mode->result.weight |= R_OUT_OF_RANGE;
			return -1;
		}

		// calculate the ratio that our scaled yres represents with respect to the original height
		y_ratio = float(yres) * y_scale / s_mode->vactive;
		int y_source_scaled = s_mode->vactive * floor(y_ratio);

		// if our original height doesn't fit the target height, we're forced to stretch
		if (!y_source_scaled)
			t_mode->result.weight |= R_RES_STRETCH;

		// otherwise we try to perform integer scaling
		else
		{
			if (t_mode->type & V_FREQ_EDITABLE)
			{
				// calculate y borders considering physical lines (instead of logical resolution)
				int tot_yres = total_lines_for_yres(yres * y_scale, vfreq_real, range, scan_factor);
				int tot_source = total_lines_for_yres(y_source_scaled, vfreq * v_scale, range, scan_factor);
				y_diff = tot_yres > tot_source?float(tot_yres % tot_source) / tot_yres * 100:0;

				// we penalize for the logical lines we need to add in order to meet the user's lower active lines limit
				int y_min = interlace == 2?range->interlaced_lines_min:range->progressive_lines_min;
				int tot_rest = (y_min >= y_source_scaled)? y_min % y_source_scaled:0;
				y_diff += float(tot_rest) / tot_yres * 100;
			}
			else
				y_diff = float((yres * y_scale) % y_source_scaled) / (yres * y_scale) * 100;

			// we save the integer ratio between source and target resolutions, this will be used for prescaling
			y_scale = floor(y_ratio);

			// now if the borders obtained are low enough (< 10%) we'll finally apply integer scaling
			// otherwise we'll stretch the original resolution over the target one
			if (!(y_ratio >= 1.0 && y_ratio < 16.0 && y_diff < 10.0))
				t_mode->result.weight |= R_RES_STRETCH;
		}
	}

	// otherwise, check if we're allowed to apply fractional scaling
	else if (t_mode->type & Y_RES_EDITABLE)
		t_mode->result.weight |= R_RES_STRETCH;

	// if there's nothing we can do, we're out of range
	else
	{
		t_mode->result.weight |= R_OUT_OF_RANGE;
		return -1;
	}

	// ··· Horizontal resolution ···
	// make the best possible adjustment of xres depending on what happened in the previous steps
	// let's start with the SCALED case
	if (!(t_mode->result.weight & R_RES_STRETCH))
	{
		// if we can, let's apply the same scaling to both directions
		if (t_mode->type & X_RES_EDITABLE)
		{
			if (t_mode->type & Y_RES_EDITABLE) yres *= y_scale;
			x_scale = y_scale;
			xres = normalize(float(xres) * float(x_scale) * cs->monitor_aspect / (cs->rotation? (1.0/(STANDARD_CRT_ASPECT)) : (STANDARD_CRT_ASPECT)), 8);
		}

		// otherwise, try to get the best out of our current xres
		else
		{
			x_scale = xres / s_mode->hactive;
			// if the source width fits our xres, try applying integer scaling
			if (x_scale)
			{
				x_scale = scale_into_aspect(s_mode->hactive, xres, cs->rotation?1.0/(STANDARD_CRT_ASPECT):STANDARD_CRT_ASPECT, cs->monitor_aspect, &x_diff);
				if (x_diff > 15.0 && t_mode->width < cs->super_width)
						t_mode->result.weight |= R_RES_STRETCH;
			}
			// otherwise apply fractional scaling
			else
				t_mode->result.weight |= R_RES_STRETCH;
		}
	}

	// if the result was fractional scaling in any of the previous steps, deal with it
	if (t_mode->result.weight & R_RES_STRETCH)
	{
		if (t_mode->type & Y_RES_EDITABLE)
		{
			// always try to use the interlaced range first if it exists, for better resolution
			yres = stretch_into_range(vfreq, range, cs->interlace, &interlace);

			// check in case we couldn't achieve the desired refresh
			vfreq_real = min(vfreq, max_vfreq_for_yres(yres, range, interlace));
		}

		// check if we can create a normal aspect resolution
		if (t_mode->type & X_RES_EDITABLE)
			xres = max(xres, normalize(STANDARD_CRT_ASPECT * yres, 8));

		// calculate integer scale for prescaling
		x_scale = max(1, scale_into_aspect(s_mode->hactive, xres, cs->rotation?1.0/(STANDARD_CRT_ASPECT):STANDARD_CRT_ASPECT, cs->monitor_aspect, &x_diff));
		y_scale = max(1, floor(float(yres) / s_mode->vactive));

		scan_factor = interlace;
		doublescan = 1;
	}

	x_ratio = float(xres) / s_mode->hactive;
	y_ratio = float(yres) / s_mode->vactive;
	v_scale = max(round_near(vfreq_real / s_mode->vfreq), 1);
	v_diff = (vfreq_real / v_scale) -  s_mode->vfreq;
	if (fabs(v_diff) > cs->refresh_tolerance)
		t_mode->result.weight |= R_V_FREQ_OFF;

	// ··· Modeline generation ···
	// compute new modeline if we are allowed to
	if (cs->modeline_generation && (t_mode->type & V_FREQ_EDITABLE))
	{
		float margin = 0;
		float vblank_lines = 0;
		float vvt_ini = 0;

		// Get games basic resolution
		t_mode->hactive = xres;
		t_mode->vactive = yres;
		t_mode->vfreq = vfreq_real;

		// Get total vertical lines
		vvt_ini = total_lines_for_yres(t_mode->vactive, t_mode->vfreq, range, scan_factor) + (interlace == 2?0.5:0);

		// Calculate horizontal frequency
		t_mode->hfreq = t_mode->vfreq * vvt_ini;

		horizontal_values:

		// Fill horizontal part of modeline
		get_line_params(t_mode, range);

		// Calculate pixel clock
		t_mode->pclock = t_mode->htotal * t_mode->hfreq;
		if (t_mode->pclock <= cs->pclock_min)
		{
			if (t_mode->type & X_RES_EDITABLE)
			{
				x_scale *= 2;
				t_mode->hactive *= 2;
				goto horizontal_values;
			}
			else
			{
				t_mode->result.weight |= R_OUT_OF_RANGE;
				return -1;
			}
		}

		// Vertical blanking
		t_mode->vtotal = vvt_ini * scan_factor;
		vblank_lines = int(t_mode->hfreq * range->vertical_blank) + (interlace == 2?0.5:0);
		margin = (t_mode->vtotal - t_mode->vactive - vblank_lines * scan_factor) / 2;
		t_mode->vbegin = t_mode->vactive + max(round_near(t_mode->hfreq * range->vfront_porch * scan_factor + margin), 1);
		t_mode->vend = t_mode->vbegin + max(round_near(t_mode->hfreq * range->vsync_pulse * scan_factor), 1);

		// Recalculate final vfreq
		t_mode->vfreq = (t_mode->hfreq / t_mode->vtotal) * scan_factor;

		t_mode->hsync = range->hsync_polarity;
		t_mode->vsync = range->vsync_polarity;
		t_mode->interlace = interlace == 2?1:0;
		t_mode->doublescan = doublescan == 1?0:1;
	}

	// finally, store result
	t_mode->result.x_scale = x_scale;
	t_mode->result.y_scale = y_scale;
	t_mode->result.v_scale = v_scale;
	t_mode->result.x_diff = x_diff;
	t_mode->result.y_diff = y_diff;
	t_mode->result.v_diff = v_diff;
	t_mode->result.x_ratio = x_ratio;
	t_mode->result.y_ratio = y_ratio;
	t_mode->result.v_ratio = 0;
	t_mode->result.rotated = cs->rotation;

	return 0;
}

//============================================================
//  get_line_params
//============================================================

int get_line_params(modeline *mode, monitor_range *range)
{
	int hhi, hhf, hht;
	int hh, hs, he, ht;
	float line_time, char_time, new_char_time;
	float hfront_porch_min, hsync_pulse_min, hback_porch_min;

	hfront_porch_min = range->hfront_porch * .90;
	hsync_pulse_min  = range->hsync_pulse  * .90;
	hback_porch_min  = range->hback_porch  * .90;

	line_time = 1 / mode->hfreq * 1000000;

	hh = round(mode->hactive / 8);
	hs = he = ht = 1;

	do {
		char_time = line_time / (hh + hs + he + ht);
		if (hs * char_time < hfront_porch_min ||
			fabs((hs + 1) * char_time - range->hfront_porch) < fabs(hs * char_time - range->hfront_porch))
			hs++;

		if (he * char_time < hsync_pulse_min ||
		    fabs((he + 1) * char_time - range->hsync_pulse) < fabs(he * char_time - range->hsync_pulse))
			he++;

		if (ht * char_time < hback_porch_min ||
		    fabs((ht + 1) * char_time - range->hback_porch) < fabs(ht * char_time - range->hback_porch))
			ht++;

		new_char_time = line_time / (hh + hs + he + ht);
	} while (new_char_time != char_time);

	hhi = (hh + hs) * 8;
	hhf = (hh + hs + he) * 8;
	hht = (hh + hs + he + ht) * 8;

	mode->hbegin  = hhi;
	mode->hend    = hhf;
	mode->htotal  = hht;

	return 0;
}

//============================================================
//  scale_into_range
//============================================================

int scale_into_range (int value, int lower_limit, int higher_limit)
{
	int scale = 1;
	while (value * scale < lower_limit) scale ++;
	if (value * scale <= higher_limit)
		return scale;
	else
		return 0;
}

//============================================================
//  scale_into_range
//============================================================

int scale_into_range (float value, float lower_limit, float higher_limit)
{
	int scale = 1;
	while (value * scale < lower_limit) scale ++;
	if (value * scale <= higher_limit)
		return scale;
	else
		return 0;
}

//============================================================
//  scale_into_aspect
//============================================================

int scale_into_aspect (int source_res, int tot_res, float original_monitor_aspect, float users_monitor_aspect, float *best_diff)
{
	int scale = 1, best_scale = 1;
	float diff = 0;
	*best_diff = 0;

	while (source_res * scale <= tot_res)
	{
		diff = fabs(1.0 - (users_monitor_aspect / (float(tot_res) / float(source_res * scale) * original_monitor_aspect))) * 100.0;
		if (diff < *best_diff || *best_diff == 0)
		{
			*best_diff = diff;
			best_scale = scale;
		}
		scale ++;
	}
	return best_scale;
}

//============================================================
//  stretch_into_range
//============================================================

int stretch_into_range(float vfreq, monitor_range *range, bool interlace_allowed, float *interlace)
{
	int yres, lower_limit;

	if (range->interlaced_lines_min && interlace_allowed)
	{
		yres = range->interlaced_lines_max;
		lower_limit = range->interlaced_lines_min;
		*interlace = 2;
	}
	else
	{
		yres = range->progressive_lines_max;
		lower_limit = range->progressive_lines_min;
	}

	while (yres > lower_limit && max_vfreq_for_yres(yres, range, *interlace) < vfreq)
		yres -= 8;

	return yres;
}


//============================================================
//  total_lines_for_yres
//============================================================

int total_lines_for_yres(int yres, float vfreq, monitor_range *range, float interlace)
{
	int vvt = max(yres / interlace + round_near(vfreq * yres / (interlace * (1.0 - vfreq * range->vertical_blank)) * range->vertical_blank), 1);
	while ((vfreq * vvt < range->hfreq_min) && (vfreq * (vvt + 1) < range->hfreq_max)) vvt++;
	return vvt;
}

//============================================================
//  max_vfreq_for_yres
//============================================================

float max_vfreq_for_yres (int yres, monitor_range *range, float interlace)
{
	return range->hfreq_max / (yres / interlace + round_near(range->hfreq_max * range->vertical_blank));
}

//============================================================
//  modeline_print
//============================================================

char * modeline_print(modeline *mode, char *modeline, int flags)
{
	char label[48]={'\x00'};
	char params[192]={'\x00'};

	if (flags & MS_LABEL)
		sprintf(label, "\"%dx%d_%d %.6fKHz %.6fHz\"", mode->hactive, mode->vactive, mode->refresh, mode->hfreq/1000, mode->vfreq);

	if (flags & MS_LABEL_SDL)
		sprintf(label, "\"%dx%d_%.6f\"", mode->hactive, mode->vactive, mode->vfreq);

	if (flags & MS_PARAMS)
		sprintf(params, " %.6f %d %d %d %d %d %d %d %d %s %s %s %s", float(mode->pclock)/1000000.0, mode->hactive, mode->hbegin, mode->hend, mode->htotal, mode->vactive, mode->vbegin, mode->vend, mode->vtotal,
			mode->interlace?"interlace":"", mode->doublescan?"doublescan":"", mode->hsync?"+hsync":"-hsync", mode->vsync?"+vsync":"-vsync");

	sprintf(modeline, "%s%s", label, params);

	return modeline;
}

//============================================================
//  modeline_result
//============================================================

char * modeline_result(modeline *mode, char *result)
{
	log_verbose("   rng(%d): ", mode->range);

	if (mode->result.weight & R_OUT_OF_RANGE)
		sprintf(result, " out of range");

	else
		sprintf(result, "%4d x%4d_%3.6f%s%s %3.6f [%s] scale(%d, %d, %d) diff(%.2f, %.2f, %.4f) ratio(%.3f, %.3f)",
			mode->hactive, mode->vactive, mode->vfreq, mode->interlace?"i":"p", mode->doublescan?"d":"", mode->hfreq/1000, mode->result.weight & R_RES_STRETCH?"fract":"integ",
			mode->result.x_scale, mode->result.y_scale, mode->result.v_scale, mode->result.x_diff, mode->result.y_diff, mode->result.v_diff, mode->result.x_ratio, mode->result.y_ratio);
	return result;
}

//============================================================
//  modeline_compare
//============================================================

int modeline_compare(modeline *t, modeline *best)
{
	bool vector = (t->hactive == (int)t->result.x_ratio);

	if (t->result.weight < best->result.weight)
		return 1;

	else if (t->result.weight <= best->result.weight)
	{
		float t_v_diff = fabs(t->result.v_diff);
		float b_v_diff = fabs(best->result.v_diff);

		if (t->result.weight & R_RES_STRETCH || vector)
		{
			float t_y_score = t->result.y_ratio * (t->interlace?(2.0/3.0):1.0);
			float b_y_score = best->result.y_ratio * (best->interlace?(2.0/3.0):1.0);

			if	((t_v_diff <  b_v_diff) ||
				((t_v_diff == b_v_diff) && (t_y_score > b_y_score)) ||
				((t_v_diff == b_v_diff) && (t_y_score == b_y_score) && (t->result.x_ratio > best->result.x_ratio)))
					return 1;
		}
		else
		{
			int t_y_score = t->result.y_scale + t->interlace + t->doublescan;
			int b_y_score = best->result.y_scale + best->interlace + best->doublescan;
			float xy_diff = roundf((t->result.x_diff + t->result.y_diff) * 100) / 100;
			float best_xy_diff = roundf((best->result.x_diff + best->result.y_diff) * 100) / 100;
			
			if	((t_y_score < b_y_score) ||
				((t_y_score == b_y_score) && (xy_diff < best_xy_diff)) ||
				((t_y_score == b_y_score) && (xy_diff == best_xy_diff) && (t->result.x_scale < best->result.x_scale)) ||
				((t_y_score == b_y_score) && (xy_diff == best_xy_diff) && (t->result.x_scale == best->result.x_scale) && (t_v_diff <  b_v_diff)))
					return 1;
		}
	}
	return 0;
}

//============================================================
//  modeline_vesa_gtf
//  Based on the VESA GTF spreadsheet by Andy Morrish 1/5/97
//============================================================

int modeline_vesa_gtf(modeline *m)
{
	int C, M;
	int v_sync_lines, v_porch_lines_min, v_front_porch_lines, v_back_porch_lines, v_sync_v_back_porch_lines, v_total_lines;
	int h_sync_width_percent, h_sync_width_pixels, h_blanking_pixels, h_front_porch_pixels, h_total_pixels;
	float v_freq, v_freq_est, v_freq_real, v_sync_v_back_porch;
	float h_freq, h_period, h_period_real, h_ideal_blanking;
	float pixel_freq, interlace;

	// Check if there's a value defined for vfreq. We're assuming input vfreq is the total field vfreq regardless interlace
	v_freq = m->vfreq? m->vfreq:float(m->refresh);

	// These values are GTF defined defaults
	v_sync_lines = 3;
	v_porch_lines_min = 1;
	v_front_porch_lines = v_porch_lines_min;
	v_sync_v_back_porch = 550;
	h_sync_width_percent = 8;
	M = 128.0 / 256 * 600;
	C = ((40 - 20) * 128.0 / 256) + 20;

	// GTF calculation
	interlace = m->interlace?0.5:0;
	h_period = ((1.0 / v_freq) - (v_sync_v_back_porch / 1000000)) / ((float)m->height + v_front_porch_lines + interlace) * 1000000;
	v_sync_v_back_porch_lines = round_near(v_sync_v_back_porch / h_period);
	v_back_porch_lines = v_sync_v_back_porch_lines - v_sync_lines;
	v_total_lines = m->height + v_front_porch_lines + v_sync_lines + v_back_porch_lines;
	v_freq_est = (1.0 / h_period) / v_total_lines * 1000000;
	h_period_real = h_period / (v_freq / v_freq_est);
	v_freq_real = (1.0 / h_period_real) / v_total_lines * 1000000;
	h_ideal_blanking = float(C - (M * h_period_real / 1000));
	h_blanking_pixels = round_near(m->width * h_ideal_blanking /(100 - h_ideal_blanking) / (2 * 8)) * (2 * 8);
	h_total_pixels = m->width + h_blanking_pixels;
	pixel_freq = h_total_pixels / h_period_real * 1000000;
	h_freq = 1000000 / h_period_real;
	h_sync_width_pixels = round_near(h_sync_width_percent * h_total_pixels / 100 / 8) * 8;
	h_front_porch_pixels = (h_blanking_pixels / 2) - h_sync_width_pixels;

	// Results
	m->hactive = m->width;
	m->hbegin = m->hactive + h_front_porch_pixels;
	m->hend = m->hbegin + h_sync_width_pixels;
	m->htotal = h_total_pixels;
	m->vactive = m->height;
	m->vbegin = m->vactive + v_front_porch_lines;
	m->vend = m->vbegin + v_sync_lines;
	m->vtotal = v_total_lines;
	m->hfreq = h_freq;
	m->vfreq = v_freq_real;
	m->pclock = pixel_freq;
	m->hsync = 0;
	m->vsync = 1;

	return true;
}

//============================================================
//  modeline_parse
//============================================================

int modeline_parse(const char *user_modeline, modeline *mode)
{
	char modeline_txt[256]={'\x00'};

	if (strcmp(user_modeline, "auto"))
	{
		// Remove quotes
		char *quote_start, *quote_end;
		quote_start = strstr((char*)user_modeline, "\"");
		if (quote_start)
		{
			quote_start++;
			quote_end = strstr(quote_start, "\"");
			if (!quote_end || *quote_end++ == 0)
				return false;
			user_modeline = quote_end;
		}

		// Get timing flags
		mode->interlace = strstr(user_modeline, "interlace")?1:0;
		mode->doublescan = strstr(user_modeline, "doublescan")?1:0;
		mode->hsync = strstr(user_modeline, "+hsync")?1:0;
		mode->vsync = strstr(user_modeline, "+vsync")?1:0;

		// Get timing values
		float pclock;
		int e = sscanf(user_modeline, " %f %d %d %d %d %d %d %d %d",
			&pclock,
			&mode->hactive, &mode->hbegin, &mode->hend, &mode->htotal,
			&mode->vactive, &mode->vbegin, &mode->vend, &mode->vtotal);

		if (e != 9)
		{
			log_error("SwitchRes: missing parameter in user modeline\n  %s\n", user_modeline);
			memset(mode, 0, sizeof(struct modeline));
			return false;
		}

		// Calculate timings
		mode->pclock = pclock * 1000000.0;
		mode->hfreq = mode->pclock / mode->htotal;
		mode->vfreq = mode->hfreq / mode->vtotal * (mode->interlace?2:1);
		mode->refresh = mode->vfreq;
		log_verbose("SwitchRes: user modeline %s\n", modeline_print(mode, modeline_txt, MS_FULL));
	}
	return true;
}

//============================================================
//  modeline_to_monitor_range
//============================================================

int modeline_to_monitor_range(monitor_range *range, modeline *mode)
{
	if (range->vfreq_min == 0)
	{
		range->vfreq_min = mode->vfreq - 0.2;
		range->vfreq_max = mode->vfreq + 0.2;
	}

	float line_time = 1 / mode->hfreq;
	float pixel_time = line_time / mode->htotal * 1000000;

	range->hfront_porch = pixel_time * (mode->hbegin - mode->hactive);
	range->hsync_pulse = pixel_time * (mode->hend - mode->hbegin);
	range->hback_porch = pixel_time * (mode->htotal - mode->hend);

	range->vfront_porch = line_time * (mode->vbegin - mode->vactive);
	range->vsync_pulse = line_time * (mode->vend - mode->vbegin);
	range->vback_porch = line_time * (mode->vtotal - mode->vend);
	range->vertical_blank = range->vfront_porch + range->vsync_pulse + range->vback_porch;

	range->hsync_polarity = mode->hsync;
	range->vsync_polarity = mode->vsync;

	range->progressive_lines_min = mode->interlace?0:mode->vactive;
	range->progressive_lines_max = mode->interlace?0:mode->vactive;
	range->interlaced_lines_min = mode->interlace?mode->vactive:0;
	range->interlaced_lines_max= mode->interlace?mode->vactive:0;

	range->hfreq_min = range->vfreq_min * mode->vtotal;
	range->hfreq_max = range->vfreq_max * mode->vtotal;

	return 1;
}

//============================================================
//  monitor_fill_vesa_gtf
//============================================================

int monitor_fill_vesa_gtf(monitor_range *range, const char *max_lines)
{
	int lines = 0;
	sscanf(max_lines, "vesa_%d", &lines);

	if (!lines)
		return 0;

	int i = 0;
	if (lines >= 480)
		i += monitor_fill_vesa_range(&range[i], 384, 480);
	if (lines >= 600)
		i += monitor_fill_vesa_range(&range[i], 480, 600);
	if (lines >= 768)
		i += monitor_fill_vesa_range(&range[i], 600, 768);
	if (lines >= 1024)
		i += monitor_fill_vesa_range(&range[i], 768, 1024);

	return i;
}

//============================================================
//  monitor_fill_vesa_range
//============================================================

int monitor_fill_vesa_range(monitor_range *range, int lines_min, int lines_max)
{
	modeline mode;
	memset(&mode, 0, sizeof(modeline));

	mode.width = real_res(STANDARD_CRT_ASPECT * lines_max);
	mode.height = lines_max;
	mode.refresh = 60;
	range->vfreq_min = 50;
	range->vfreq_max = 65;

	modeline_vesa_gtf(&mode);
	modeline_to_monitor_range(range, &mode);

	range->progressive_lines_min = lines_min;
	range->hfreq_min = mode.hfreq - 500;
	range->hfreq_max = mode.hfreq + 500;
	monitor_show_range(range);

	return 1;
}

//============================================================
//  round_near
//============================================================

int round_near(double number)
{
    return number < 0.0 ? ceil(number - 0.5) : floor(number + 0.5);
}

//============================================================
//  normalize
//============================================================

int normalize(int a, int b)
{
	int c, d;
	c = a % b;
	d = a / b;
	if (c) d++;
	return d * b;
}

//============================================================
//  real_res
//============================================================

int real_res(int x) {return (int) (x / 8) * 8;}
