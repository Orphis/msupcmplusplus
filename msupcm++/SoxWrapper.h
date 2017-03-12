#pragma once
#include "sox_main.h"
#include <string>

namespace msu
{
	class SoxWrapper
	{
	public:
		SoxWrapper();
		~SoxWrapper();

		bool init(std::string in, std::string out);
		bool addInput(std::string name);
		bool setCombine(sox_combine_method method);
		bool trim(unsigned start, unsigned end = 0);
		bool fade(unsigned in, unsigned out = 0, char type = 't');
		bool pad(unsigned start, unsigned end = 0);
		bool tempo(double tempo);
		bool setLoop(size_t loop);
		bool finalize();
		bool clear();

	private:
		bool addOutput(std::string name);
		bool addEffect(std::string name, int argc, char** argv);
		std::string getTempFile(std::string ext);

		bool m_initialized;
		bool m_finalized;
		int m_temp_counter;
		size_t m_loop;

		std::string m_output;
	};

	class SoxWrapperFactory
	{
	public:
		~SoxWrapperFactory() { delete(m_instance); }
		static SoxWrapper* getInstance()
		{
			if (!m_instance)
				m_instance = new SoxWrapper();

			return m_instance;
		}

	private:
		SoxWrapperFactory();
		static SoxWrapper *m_instance;
	};
}