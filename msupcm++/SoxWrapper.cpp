#include "SoxWrapper.h"
#include "GlobalConfig.h"
#include "util.h"
#include <stdio.h>
#include <sys/stat.h>

#define TEMP_FILE_PREFIX "__sox_wrapper_temp__"

using namespace msu;

SoxWrapper* SoxWrapperFactory::m_instance(0);

SoxWrapper::SoxWrapper() :
	m_initialized(false), m_finalized(false), m_temp_counter(0), m_loop(0)
{
	sox_init();
	sox_get_globals()->verbosity = GlobalConfig::verbosity();
}


SoxWrapper::~SoxWrapper()
{
	clear();
	if (!cleanup_called)
		cleanup();
}

bool SoxWrapper::init(std::string in, std::string out)
{
	if (!clear())
		return m_initialized = false;

	if (!addInput(in))
		return m_initialized = false;

	m_output = out;
	combine_method = sox_default;

	// DO NOT CALL add_eff_chain()!!
	// Since we need to manually reallocate the arrays any time
	// we add an effect, mixing lsx_revalloc's with C++ allocations
	// causes heap corruption and all kinds of random headaches
	// so just handle these arrays manually in our code
	//add_eff_chain();
	user_effargs = new user_effargs_t*[1];
	user_effargs[0] = new user_effargs_t;

	user_effargs_size = new size_t[1];
	user_effargs_size[0] = 0;
	nuser_effects = new size_t[1];
	nuser_effects[0] = 0;
	eff_chain_count = 1;

	m_loop = 0;
	m_length = 0;

	return m_initialized = true;
}


bool SoxWrapper::addInput(std::string name)
{
	file_t opts;

	// Check if file exists
	struct stat file;
	if (!stat(name.c_str(), &file) == 0)
		return false;

	init_file(&opts);
	add_file(&opts, name.c_str());
	++input_count;
	return true;
}


bool SoxWrapper::setCombine(sox_combine_method method)
{
	combine_method = method;
	return true;
}


bool SoxWrapper::trim(size_t start, size_t end)
{
	char* args[2];
	bool ret;

	if (!m_initialized || m_finalized)
		return false;

	if (start > 0 || end > 0)
	{
		args[0] = new char[32];
		args[1] = new char[32];

		if (start > 0)
		{
			strncpy(args[0], std::to_string(start).append("s").c_str(), 32);
		}
		else
		{
			strncpy(args[0], "0s", 32);
		}

		if (end > 0)
		{
			strncpy(args[1], std::string("=").append(std::to_string(end)).append("s").c_str(), 32);
			ret = addEffect("trim", 2, (char**)args);
		}
		else
		{
			ret = addEffect("trim", 1, (char**)args);
		}

		delete args[0];
		delete args[1];

		return ret;
	}

	return false;
}


bool SoxWrapper::fade(size_t in, size_t out, char type)
{
	char* args[4];
	bool ret;

	if (!m_initialized || m_finalized)
		return false;

	if (std::string("qhtlp").find(type) == std::string::npos)
		return false;

	if (in > 0 || out > 0)
	{
		args[0] = new char[2] { type, '\0' };
		args[1] = new char[32];
		args[2] = new char[3] { '-', '0', '\0' };
		args[3] = new char[32];

		if (in > 0)
		{
			strncpy(args[1], std::to_string(in).append("s").c_str(), 32);
		}
		else
		{
			strncpy(args[1], "0", 32);
		}

		if (out > 0)
		{

			strncpy(args[3], std::to_string(out).append("s").c_str(), 32);
			ret = addEffect("fade", 4, (char**)args);
		}
		else
		{
			ret = addEffect("fade", 2, (char**)args);
		}

		delete args[0];
		delete args[1];
		delete args[2];
		delete args[3];

		return ret;
	}

	return false;
}


bool SoxWrapper::pad(size_t start, size_t end)
{
	char* args[2];
	bool ret;

	if (!m_initialized || m_finalized)
		return false;

	if (start > 0 || end > 0)
	{
		args[0] = new char[32];
		args[1] = new char[32];

		if (start > 0)
		{
			strncpy(args[0], std::to_string(start).append("s").c_str(), 32);
		}
		else
		{
			strncpy(args[0], "0s", 32);
		}

		if (end > 0)
		{
			strncpy(args[1], std::to_string(end).append("s").c_str(), 32);
			ret = addEffect("pad", 2, (char**)args);
		}
		else
		{
			ret = addEffect("pad", 1, (char**)args);
		}

		delete args[0];
		delete args[1];

		return ret;
	}

	return false;
}


bool SoxWrapper::tempo(double tempo)
{
	char* args[2];
	bool ret;

	if (!m_initialized || m_finalized)
		return false;

	if (tempo < 0)
		return false;

	args[0] = new char[3]{ '-', 'm', '\0' };
	args[1] = new char[32];
	
	strncpy(args[1], std::to_string(tempo).c_str(), 32);
	ret = addEffect("tempo", 2, (char**)args);

	delete args[0];
	delete args[1];

	return ret;
}


bool SoxWrapper::setLoop(size_t loop)
{
	m_loop = loop;
	return true;
}


bool SoxWrapper::crossFade(size_t loop, size_t end, size_t length, double ratio)
{
	if (length <= 0)
		return false;

	std::string temp1, temp2, temp3;
	std::string final_output = m_output;
	m_output = temp1 = getTempFile("wav");

	finalize();

	// Hacky workaround to keep temp1 for 2 separate passes
	bool keep_temps = GlobalConfig::keep_temps();
	GlobalConfig::keep_temps() = true;

	init(temp1, temp2 = getTempFile("wav"));
	trim(0, end);
	fade(0, length * ratio);
	finalize();

	GlobalConfig::keep_temps() = keep_temps;

	init(temp1, temp3 = getTempFile("wav"));
	trim(loop > length ? loop - length : 0, loop);
	fade(loop > length ? length : loop);
	pad(loop > length ? end - length : end - loop);
	finalize();

	init(temp2, final_output);
	addInput(temp3);
	combine_method = sox_mix;

	return true;
}


bool SoxWrapper::normalize(double level)
{
	char* args[1];
	bool ret;

	if (!m_initialized || m_finalized)
		return false;

	if (level == 0.0)
		return false;

	args[0] = new char[32];

	snprintf(args[0], 32, "%.2f", level);
	ret = addEffect("nongnunormalize", 1, (char**)args);

	delete args[0];

	return ret;
}


bool SoxWrapper::finalize()
{
	if (GlobalConfig::dither())
	{
		char* dither_args[1];

		dither_args[0] = new char[3]{ '-','s','\0' };
		addEffect("dither", 1, (char**)dither_args);

		delete dither_args[0];
	}

	addOutput(m_output);

	sox_format_handler_t const * handler =
		sox_write_handler(ofile->filename, ofile->filetype, NULL);

	if (combine_method == sox_default)
		combine_method = sox_concatenate;

	/* Allow e.g. known length processing in this case */
	if (combine_method == sox_sequence && input_count == 1)
		combine_method = sox_concatenate;

	/* Make sure we got at least the required # of input filenames */
	if (input_count < (size_t)(is_serial(combine_method) ? 1 : 2))
		usage("Not enough input files specified");

	for (auto i = 0; i < input_count; i++) {
		size_t j = input_count - 1 - i; /* Open in reverse order 'cos of rec (below) */
		file_t * f = files[j];

		/* When mixing audio, default to input side volume adjustments that will
		* make sure no clipping will occur.  Users probably won't be happy with
		* this, and will override it, possibly causing clipping to occur. */
		if (combine_method == sox_mix && !uservolume)
			f->volume = 1.0 / input_count;
		else if (combine_method == sox_mix_power && !uservolume)
			f->volume = 1.0 / sqrt((double)input_count);

		files[j]->ft = sox_open_read(f->filename, &f->signal, &f->encoding, f->filetype);
		if (!files[j]->ft)
			/* sox_open_read() will call lsx_warn for most errors.
			* Rely on that printing something. */
			exit(2);
		if (show_progress == sox_option_default &&
			(files[j]->ft->handler.flags & SOX_FILE_DEVICE) != 0 &&
			(files[j]->ft->handler.flags & SOX_FILE_PHONY) == 0)
			show_progress = sox_option_yes;
	}

	/* Not the best way for users to do this; now deprecated in favour of soxi. */
	if (!show_progress && !nuser_effects[current_eff_chain] &&
		ofile->filetype && !strcmp(ofile->filetype, "null")) {
		for (auto i = 0; i < input_count; i++)
			report_file_info(files[i]);
		exit(0);
	}

	if (!sox_globals.repeatable) {/* Re-seed PRNG? */
		struct timeval now;
		gettimeofday(&now, NULL);
		sox_globals.ranqd1 = (int32_t)(now.tv_sec - now.tv_usec);
	}

	/* Set output file options */
	ofile->signal.channels = 2;
	ofile->signal.rate = 44100;
	ofile->encoding.bits_per_sample = 16;

	/* Save things that sox_sequence needs to be reinitialised for each segued
	* block of input files.*/
	ofile_signal_options = ofile->signal;
	ofile_encoding_options = ofile->encoding;

	/* Private data for PCM file */
	typedef struct {
		size_t loop_point;
		size_t remaining_samples;
	} priv_t;

	while (process() != SOX_EOF && !user_abort && current_input < input_count)
	{
		if (advance_eff_chain() == SOX_EOF)
			break;

		if (!save_output_eff)
		{
			m_length = ofile->ft->olength / 2;
			sox_close(ofile->ft);
			ofile->ft = NULL;
		}
	}

	if(ofile->ft != NULL)
		m_length = ofile->ft->olength / 2;

	if (strncmp(ofile->ft->filetype, "pcm", 3) == 0)
	{
		((priv_t*)ofile->ft->priv)->loop_point = m_loop;
	}

	m_finalized = true;;
	return clear();
}


bool SoxWrapper::clear()
{
	if (!m_initialized && !m_finalized)
		return true;

	sox_delete_effects_chain(effects_chain);
	effects_chain = NULL;

	// DO NOT CALL delete_eff_chains()!!
	// for the same reason we don't call add_eff_chain()
	//delete_eff_chains();
	for (auto i = 0; i < eff_chain_count; ++i)
	{
		delete[] user_effargs[i];
	}
	delete[] user_effargs;
	delete[] user_effargs_size;
	delete[] nuser_effects;
	eff_chain_count = 0;
	current_eff_chain = 0;
	user_effargs = NULL;
	user_effargs_size = NULL;
	nuser_effects = NULL;
	user_efftab = NULL;
	user_efftab_size = 0;

	for (auto i = 0; i < file_count; ++i)
		if (files[i]->ft->clips != 0)
			lsx_warn(i < input_count ? "`%s' input clipped %" PRIu64 " samples" :
				"`%s' output clipped %" PRIu64 " samples; decrease volume?",
				(files[i]->ft->handler.flags & SOX_FILE_DEVICE) ?
				files[i]->ft->handler.names[0] : files[i]->ft->filename,
				files[i]->ft->clips);

	if (mixing_clips > 0)
		lsx_warn("mix-combining clipped %" PRIu64 " samples; decrease volume?", mixing_clips);

	for (auto i = 0; i < file_count; i++)
		if (files[i]->volume_clips > 0)
			lsx_warn("`%s' balancing clipped %" PRIu64 " samples; decrease volume?",
				files[i]->filename, files[i]->volume_clips);

	if (show_progress) {
		if (user_abort)
			fprintf(stderr, "Aborted.\n");
		else if (user_skip && sox_mode != sox_rec)
			fprintf(stderr, "Skipped.\n");
		else
			fprintf(stderr, "Done.\n");
	}

	success = 1; /* Signal success to cleanup so the output file isn't removed. */

	/* Close the input and output files before exiting. */
	for (auto i = 0; i < input_count; i++) {
		if (files[i]->ft) {
			sox_close(files[i]->ft);
		}

		if (!GlobalConfig::keep_temps())
		{
			if (strlen(files[i]->filename) > strlen(TEMP_FILE_PREFIX) &&
				strncmp(files[i]->filename, TEMP_FILE_PREFIX,
				strlen(TEMP_FILE_PREFIX)) == 0)
				remove(files[i]->filename);
		}

		free(files[i]->filename);
		free(files[i]);
	}

	if (file_count) {
		if (ofile->ft) {
			if (!success && ofile->ft->io_type == lsx_io_file) {   /* If we failed part way through */
				struct stat st;                  /* writing a normal file, remove it. */
				if (!stat(ofile->ft->filename, &st) &&
					(st.st_mode & S_IFMT) == S_IFREG)
					unlink(ofile->ft->filename);
			}
			sox_close(ofile->ft); /* Assume we can unlink a file before closing it. */
		}
		free(ofile->filename);
		free(ofile);
	}

	free(files);

	input_count = 0;
	current_input = 0;
	file_count = 0;
	files = NULL;
	m_output.clear();
	m_loop = 0;
	m_initialized = false;
	m_finalized = false;
	return true;
}


size_t SoxWrapper::length()
{
	return m_length;
}


bool SoxWrapper::addOutput(std::string name)
{
	file_t opts;
	init_file(&opts);

	add_file(&opts, name.c_str());
	return true;
}


bool SoxWrapper::addEffect(std::string name, int argc, char** argv)
{
	size_t eff_offset;
	size_t last_chain = eff_chain_count - 1;

	eff_offset = nuser_effects[last_chain];
	if (eff_offset == user_effargs_size[last_chain]) {
		user_effargs_size[last_chain] += EFFARGS_STEP;
		user_effargs_t* ef = user_effargs[last_chain];
		user_effargs[last_chain] = new user_effargs_t[user_effargs_size[last_chain]];
		for (auto i = 0; i < EFFARGS_STEP; ++i)
		{
			if (i < eff_offset)
			{
				user_effargs[last_chain][i] = ef[i];
			}
			else
			{
				user_effargs[last_chain][i].argv = NULL;
				user_effargs[last_chain][i].argv_size = 0;
			}
		}
		delete[] ef;
	}

	/* Name should always be correct! */
	user_effargs[last_chain][eff_offset].name = lsx_strdup(name.c_str());
	while (user_effargs[last_chain][eff_offset].argv_size < argc)
		user_effargs[last_chain][eff_offset].argv_size += EFFARGS_STEP;

	user_effargs[last_chain][eff_offset].argv = new char*[user_effargs[last_chain][eff_offset].argv_size];

	for (auto i = 0; i < argc; ++i)
	{
		user_effargs[last_chain][eff_offset].argv[i] = lsx_strdup(argv[i]);
	}
	user_effargs[last_chain][eff_offset].argc = argc;
	nuser_effects[last_chain]++;

	return true;
}


std::string SoxWrapper::getTempFile(std::string ext)
{
	return std::string(TEMP_FILE_PREFIX).append(std::to_string(m_temp_counter++)).append(".").append(ext);
}
