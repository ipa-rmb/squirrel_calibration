/****************************************************************
 *
 * Copyright (c) 2015
 *
 * Fraunhofer Institute for Manufacturing Engineering
 * and Automation (IPA)
 *
 * +++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
 *
 * Project name: squirrel
 * ROS stack name: squirrel_calibration
 * ROS package name: robotino_calibration
 *
 * +++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
 *
 * Author: Richard Bormann, email:richard.bormann@ipa.fhg.de
 *
 * Date of creation: December 2015
 *
 * +++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of the Fraunhofer Institute for Manufacturing
 *       Engineering and Automation (IPA) nor the names of its
 *       contributors may be used to endorse or promote products derived from
 *       this software without specific prior written permission.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License LGPL as
 * published by the Free Software Foundation, either version 3 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License LGPL for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License LGPL along with this program.
 * If not, see <http://www.gnu.org/licenses/>.
 *
 ****************************************************************/

#include <ros/ros.h>
#include <robotino_calibration/camera_base_calibration_checkerboard.h>
#include <robotino_calibration/camera_base_calibration_pitag.h>
#include <map>

//#######################
//#### main programm ####
int main(int argc, char** argv)
{
	// Initialize ROS, specify name of node
	ros::init(argc, argv, "camera_base_calibration");

	// Create a handle for this node, initialize node
	ros::NodeHandle nh("~");

	// load parameters
	std::string marker_type;
	bool load_images = false;
	std::cout << "\n========== Relative Localization Parameters ==========\n";
	nh.param<std::string>("marker_type", marker_type, "");
	std::cout << "marker_type: " << marker_type << std::endl;
	nh.param("load_images", load_images, false);
	std::cout << "load_images: " << load_images << std::endl;

	try
	{
		if (marker_type.compare("checkerboard") == 0)
		{
			CameraBaseCalibrationCheckerboard cb(nh);
			cb.calibrateCameraToBase(load_images);
		}
		else if (marker_type.compare("pitag") == 0)
		{
			CameraBaseCalibrationPiTag pt(nh);
			pt.calibrateCameraToBase(load_images);
		}
	}
	catch ( std::exception &e )
	{
		return -1;
	}

	return 0;
}
