#ifndef csv_logger_H
#define csv_logger_H

#include <string>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <boost/filesystem.hpp>
#include <sstream>


#include "spdlog/spdlog.h"
#include "spdlog/cfg/env.h"

#include "configuration.h"


class csv_logger{
	private:


		/** Directory to which log files will be written **/
		std::string _file_directory;

		/** Log file prefix**/
		std::string _file_name;

		/** Maximum log file size in Megabytes **/
		int _log_file_size_inMB_max;
		
		/** Open Log file **/
		std::ofstream _log_file;

		/** Open Log file name **/
		std::string _cur_file_name;


		/**
		 * Method returns string representation of current date and time.
		 * 
		 * @return std::string representing current time and date 
		 **/
		std::string GetCurDateTimeStr() const;

		/**
		 * On initial call method will create a logs directory under the _file_directory path specified and 
		 * open a logfile with the naming convention _file_name.csv. On subsequent calls, method will close
		 * current log file, rename it with current date and timestamp and move it into the logs directory.
		 * Method will then open a new file. 
		 * 
		 **/ 
		void openLogFile();

		/**
		 * Helper method to handle renaming and moving logic. Includes a check for existing files with a
		 * given new proposed name. Will append "(1)" to any filename that already exists.
		 * 
		 **/ 
		void renameAndMoveLogfile() const;

		/**
		 * Method to checkfile size against configurable limit. If filesize exceeds limit log file is rotated.
		 **/
		void fileSizeCheck();

	public:

		/** Constructor **/
		explicit csv_logger( std::string const &log_directory, std::string const &log_filename, int filesize_max);

		/** Deconstructor **/
		~csv_logger();

		/**
		 * Log new line into csv file.
		 * 
		 * @param new_line to log into current csv log file.
		 **/ 
		void log_line( std::string const &new_line );

};


#endif