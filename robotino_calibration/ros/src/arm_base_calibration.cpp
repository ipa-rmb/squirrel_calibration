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
 * Author: Marc Riedlinger, email:marc.riedlinger@ipa.fraunhofer.de
 *
 * Date of creation: September 2016
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


#include <robotino_calibration/arm_base_calibration.h>
#include <robotino_calibration/transformation_utilities.h>
#include <std_msgs/Float64.h>
#include <pcl/point_types.h>
#include <pcl/registration/icp.h>
#include <boost/filesystem.hpp>
#include <sstream>
#include <fstream>

ArmBaseCalibration::ArmBaseCalibration(ros::NodeHandle nh) :
		node_handle_(nh), transform_listener_(nh), arm_calibration_path_("robotino_calibration/arm_calibration/"), calibrated_(false)
{
	// create data storage path if it does not yet exist
	boost::filesystem::path storage_path(arm_calibration_path_);
	if (boost::filesystem::exists(storage_path) == false)
	{
		if (boost::filesystem::create_directories(storage_path) == false && boost::filesystem::exists(storage_path) == false)
		{
			std::cout << "Error: ArmBaseCalibration::ArmBaseCalibration: Could not create directory " << storage_path << std::endl;
			return;
		}
	}

	// load parameters
	std::cout << "\n========== ArmBaseCalibration Parameters ==========\n";

	// load parameters
	node_handle_.param("chessboard_cell_size", chessboard_cell_size_, 0.05);
	std::cout << "chessboard_cell_size: " << chessboard_cell_size_ << std::endl;
	chessboard_pattern_size_ = cv::Size(6,4);
	std::vector<double> temp;
	node_handle_.getParam("chessboard_pattern_size", temp);
	if (temp.size() == 2)
		chessboard_pattern_size_ = cv::Size(temp[0], temp[1]);
	std::cout << "pattern: " << chessboard_pattern_size_ << std::endl;
	node_handle_.param<std::string>("checkerboard_frame", checkerboard_frame_, "marker");
	std::cout << "checkerboard_frame: " << checkerboard_frame_ << std::endl;

	// coordinate frame name parameters
	node_handle_.param<std::string>("base_frame", base_frame_, "base_link");
	std::cout << "base_frame: " << base_frame_ << std::endl;
	node_handle_.param<std::string>("armbase_frame", armbase_frame_, "");
	std::cout << "armbase_frame: " << armbase_frame_ << std::endl;
	node_handle_.param<std::string>("endeff_frame_", endeff_frame_, "");
	std::cout << "endeff_frame: " << endeff_frame_ << std::endl;

	// initial parameters
	temp.clear();
	T_base_to_armbase_ = robotino_calibration::makeTransform(robotino_calibration::rotationMatrixFromYPR(0.0, 0.0, 0.0), cv::Mat(cv::Vec3d(0.0, 0.0, 0.0)));
	node_handle_.getParam("T_base_to_armbase_initial", temp);
	if ( temp.size()==6 )
		T_base_to_armbase_ = robotino_calibration::makeTransform(robotino_calibration::rotationMatrixFromYPR(temp[3], temp[4], temp[5]), cv::Mat(cv::Vec3d(temp[0], temp[1], temp[2])));
	std::cout << "T_base_to_arm_initial:\n" << T_base_to_armbase_ << std::endl;
	temp.clear();

	T_endeff_to_checkerboard_ = robotino_calibration::makeTransform(robotino_calibration::rotationMatrixFromYPR(0.0, 0.0, 0.0), cv::Mat(cv::Vec3d(0.0, 0.0, 0.0)));
	node_handle_.getParam("T_endeff_to_checkerboard_initial", temp);
	if ( temp.size()==6 )
		T_endeff_to_checkerboard_ = robotino_calibration::makeTransform(robotino_calibration::rotationMatrixFromYPR(temp[3], temp[4], temp[5]), cv::Mat(cv::Vec3d(temp[0], temp[1], temp[2])));
	std::cout << "T_endeff_to_checkerboard_initial:\n" << T_endeff_to_checkerboard_ << std::endl;

	// optimization parameters
	node_handle_.param("optimization_iterations", optimization_iterations_, 100);
	std::cout << "optimization_iterations: " << optimization_iterations_ << std::endl;
	// pan/tilt unit positions and robot base locations relative to marker
	bool use_range = false;
	node_handle_.param("use_range", use_range, false);
	std::cout << "use_range: " << use_range << std::endl;
	if (use_range == true)
	{
		// create robot configurations from regular grid
		std::vector<double> x_range;
		node_handle_.getParam("x_range", x_range);
		std::vector<double> y_range;
		node_handle_.getParam("y_range", y_range);
		std::vector<double> phi_range;
		node_handle_.getParam("phi_range", phi_range);
		std::vector<double> pan_range;
		node_handle_.getParam("pan_range", pan_range);
		std::vector<double> tilt_range;
		node_handle_.getParam("tilt_range", tilt_range);
		if (x_range.size()!=3 || y_range.size()!=3 || phi_range.size()!=3 || pan_range.size()!=3 || tilt_range.size()!=3)
		{
			ROS_ERROR("One of the range vectors has wrong size.");
			return;
		}
		if (x_range[0] == x_range[2] || x_range[1] == 0.)		// this sets the step to something bigger than 0
			x_range[1] = 1.0;
		if (y_range[0] == y_range[2] || y_range[1] == 0.)
			y_range[1] = 1.0;
		if (phi_range[0] == phi_range[2] || phi_range[1] == 0.)
			phi_range[1] = 1.0;
		if (pan_range[0] == pan_range[2] || pan_range[1] == 0.)
			pan_range[1] = 1.0;
		if (tilt_range[0] == tilt_range[2] || tilt_range[1] == 0.)
			tilt_range[1] = 1.0;
		for (double x=x_range[0]; x<=x_range[2]; x+=x_range[1])
			for (double y=y_range[0]; y<=y_range[2]; y+=y_range[1])
				for (double phi=phi_range[0]; phi<=phi_range[2]; phi+=phi_range[1])
					for (double pan=pan_range[0]; pan<=pan_range[2]; pan+=pan_range[1])
						for (double tilt=tilt_range[0]; tilt<=tilt_range[2]; tilt+=tilt_range[1])
							arm_configurations_.push_back(RobotConfiguration(x, y, phi, pan, tilt));
	}
	else
	{
		// read out user-defined robot configurations
		temp.clear();
		node_handle_.getParam("robot_configurations", temp);
		const int number_configurations = temp.size()/5;
		if (temp.size()%5 != 0 || temp.size() < 3*5)
		{
			ROS_ERROR("The robot_configurations vector should contain at least 3 configurations with 5 values each.");
			return;
		}
		std::cout << "Robot configurations:\n";
		for (int i=0; i<number_configurations; ++i)
		{
			arm_configurations_.push_back(RobotConfiguration(temp[5*i], temp[5*i+1], temp[5*i+2], temp[5*i+3], temp[5*i+4]));
			std::cout << temp[5*i] << "\t" << temp[5*i+1] << "\t" << temp[5*i+2] << "\t" << temp[5*i+3] << "\t" << temp[5*i+4] << std::endl;
		}
	}

	// topics
	/*pan_tilt_state_ = node_handle_.subscribe<sensor_msgs::JointState>("/pan_tilt_controller/joint_states", 0, &CameraBaseCalibrationMarker::panTiltJointStateCallback, this);
	tilt_controller_ = node_handle_.advertise<std_msgs::Float64>(tilt_controller_command_, 1, false);
	pan_controller_ = node_handle_.advertise<std_msgs::Float64>(pan_controller_command_, 1, false);
	base_controller_ = node_handle_.advertise<geometry_msgs::Twist>("/cmd_vel", 1, false);*/

	// set up messages
	it_ = new image_transport::ImageTransport(node_handle_);
	color_image_sub_.subscribe(*it_, "colorimage_in", 1);
	color_image_sub_.registerCallback(boost::bind(&ArmBaseCalibration::imageCallback, this, _1));

	ROS_INFO("CameraBaseCalibration initialized.");
}

ArmBaseCalibration::~ArmBaseCalibration()
{
	if (it_ != 0)
		delete it_;
}

void ArmBaseCalibration::setCalibrationStatus(bool calibrated)
{
		calibrated_ = calibrated;
}

bool ArmBaseCalibration::convertImageMessageToMat(const sensor_msgs::Image::ConstPtr& image_msg, cv_bridge::CvImageConstPtr& image_ptr, cv::Mat& image)
{
	try
	{
		image_ptr = cv_bridge::toCvShare(image_msg, sensor_msgs::image_encodings::BGR8);//image_msg->encoding);
	}
	catch (cv_bridge::Exception& e)
	{
		ROS_ERROR("ImageFlip::convertColorImageMessageToMat: cv_bridge exception: %s", e.what());
		return false;
	}
	image = image_ptr->image;

	return true;
}

void ArmBaseCalibration::imageCallback(const sensor_msgs::ImageConstPtr& color_image_msg)
{
	// secure this access with a mutex
	boost::mutex::scoped_lock lock(camera_data_mutex_);

	if (capture_image_ == true)
	{
		// read image
		cv_bridge::CvImageConstPtr color_image_ptr;
		if (convertImageMessageToMat(color_image_msg, color_image_ptr, camera_image_) == false)
			return;

		latest_image_time_ = color_image_msg->header.stamp;

		capture_image_ = false;
	}
}

bool ArmBaseCalibration::calibrateArmToBase(const bool load_images)
{
	// setup storage folder
	//int return_value = system("mkdir -p robotino_calibration/camera_calibration");

	// pre-cache images
	if (load_images == false)
	{
		ros::spinOnce();
		ros::Duration(2).sleep();
		capture_image_ = true;
		ros::spinOnce();
		ros::Duration(2).sleep();
		capture_image_ = true;
	}

	// acquire images
	int image_width=0, image_height=0;
	std::vector< std::vector<cv::Point2f> > points_2d_per_image;
	std::vector<cv::Mat> T_base_to_checkerboard_vector;
	std::vector<cv::Mat> T_armbase_to_endeff_vector;
	//std::vector<cv::Mat> T_camera_to_camera_optical_vector;
	acquireCalibrationImages(arm_configurations_, chessboard_pattern_size_, load_images, image_width, image_height, points_2d_per_image, T_base_to_checkerboard_vector,
			T_armbase_to_endeff_vector);

	// prepare chessboard 3d points
	std::vector< std::vector<cv::Point3f> > pattern_points_3d;
	computeCheckerboard3dPoints(pattern_points_3d, chessboard_pattern_size_, chessboard_cell_size_, points_2d_per_image.size());

	// extrinsic calibration between base and torso_lower as well as torso_upper and camera
	for (int i=0; i<optimization_iterations_; ++i)
	{
		extrinsicCalibrationBaseToArm(pattern_points_3d, T_base_to_checkerboard_vector, T_armbase_to_endeff_vector /*, T_camera_to_checkerboard_vector*/);
		extrinsicCalibrationEndeffToCheckerboard(pattern_points_3d, T_base_to_checkerboard_vector, T_armbase_to_endeff_vector);
	}

	// display calibration parameters
	displayAndSaveCalibrationResult(T_base_to_armbase_);

	// save calibration
	saveCalibration();
	calibrated_ = true;

	return true;
}

bool ArmBaseCalibration::moveArm(const RobotConfiguration& arm_configuration)
{

}

bool ArmBaseCalibration::acquireCalibrationImages(const std::vector<RobotConfiguration>& robot_configurations,
		const cv::Size pattern_size, const bool load_images, int& image_width, int& image_height,
		std::vector< std::vector<cv::Point2f> >& points_2d_per_image, std::vector<cv::Mat>& T_base_to_checkerboard_vector,
		std::vector<cv::Mat>& T_armbase_to_endeff_vector)
{
	// capture images from different perspectives
	const int number_images_to_capture = (int)robot_configurations.size();
	for (int image_counter = 0; image_counter < number_images_to_capture; ++image_counter)
	{
		if (!load_images)
			moveArm(robot_configurations[image_counter]);

		// acquire image and extract checkerboard points
		std::vector<cv::Point2f> checkerboard_points_2d;
		int return_value = acquireCalibrationImage(image_width, image_height, checkerboard_points_2d, pattern_size, load_images, image_counter);
		if (return_value != 0)
			continue;

		// retrieve transformations
		cv::Mat T_base_to_checkerboard, T_armbase_to_endeff;
		std::stringstream path;
		path << arm_calibration_path_ << image_counter << ".yml";
		if (load_images)
		{
			cv::FileStorage fs(path.str().c_str(), cv::FileStorage::READ);
			if (fs.isOpened())
			{
				fs["T_base_to_checkerboard"] >> T_base_to_checkerboard;
				fs["T_armbase_to_endeff"] >> T_armbase_to_endeff;
			}
			else
			{
				ROS_WARN("Could not read transformations from file '%s'.", path.str().c_str());
				continue;
			}
			fs.release();
		}
		else
		{
			bool result = true;
			result &= robotino_calibration::getTransform(transform_listener_, base_frame_, checkerboard_frame_, T_base_to_checkerboard);
			//result &= robotino_calibration::getTransform(transform_listener_, armbase_frame_, endeff_frame_, T_armbase_to_endeff);

			if (result == false)
				continue;

			// save transforms to file
			cv::FileStorage fs(path.str().c_str(), cv::FileStorage::WRITE);
			if (fs.isOpened())
			{
				fs << "T_base_to_checkerboard" << T_base_to_checkerboard;
				fs << "T_armbase_to_endeff" << T_armbase_to_endeff;
			}
			else
			{
				ROS_WARN("Could not write transformations to file '%s'.", path.str().c_str());
				continue;
			}
			fs.release();
		}

		points_2d_per_image.push_back(checkerboard_points_2d);
		T_base_to_checkerboard_vector.push_back(T_base_to_checkerboard);
		T_armbase_to_endeff_vector.push_back(T_armbase_to_endeff);
		std::cout << "Captured perspectives: " << points_2d_per_image.size() << std::endl;
	}

	return true;
}

int ArmBaseCalibration::acquireCalibrationImage(int& image_width, int& image_height,
		std::vector<cv::Point2f>& checkerboard_points_2d, const cv::Size pattern_size, const bool load_images, int& image_counter)
{
	int return_value = 0;

	// acquire image
	cv::Mat image;
	if (load_images == false)
	{
		ros::Duration(3).sleep();
		capture_image_ = true;
		ros::spinOnce();
		ros::Duration(2).sleep();

		// retrieve image from camera
		{
			boost::mutex::scoped_lock lock(camera_data_mutex_);

			std::cout << "Time diff: " << (ros::Time::now() - latest_image_time_).toSec() << std::endl;

			if ((ros::Time::now() - latest_image_time_).toSec() < 20.0)
			{
				image = camera_image_.clone();
			}
			else
			{
				ROS_WARN("Did not receive camera images recently.");
				return -1;		// -1 = no fresh image available
			}
		}
	}
	else
	{
		// load image from file
		std::stringstream ss;
		ss << arm_calibration_path_ << image_counter;
		std::string image_name = ss.str() + ".png";
		image = cv::imread(image_name.c_str(), 0);
		if (image.empty())
			return -2;
	}
	image_width = image.cols;
	image_height = image.rows;

	// find pattern in image
	bool pattern_found = cv::findChessboardCorners(image, pattern_size, checkerboard_points_2d, cv::CALIB_CB_FAST_CHECK + cv::CALIB_CB_FILTER_QUADS);

	// display
	cv::Mat display = image.clone();
	cv::drawChessboardCorners(display, pattern_size, cv::Mat(checkerboard_points_2d), pattern_found);
	cv::imshow("image", display);
	cv::waitKey(50);

	// collect 2d points
	if (checkerboard_points_2d.size() == pattern_size.height*pattern_size.width)
	{
		// save images
		if (load_images == false)
		{
			std::stringstream ss;
			ss << arm_calibration_path_ << image_counter;
			std::string image_name = ss.str() + ".png";
			cv::imwrite(image_name.c_str(), image);
		}
	}
	else
	{
		ROS_WARN("Not all checkerboard points have been observed.");
		return_value = -2;
	}

	return return_value;
}

void ArmBaseCalibration::computeCheckerboard3dPoints(std::vector< std::vector<cv::Point3f> >& pattern_points, const cv::Size pattern_size, const double chessboard_cell_size, const int number_images)
{
	// prepare chessboard 3d points
	pattern_points.clear();
	pattern_points.resize(1);
	pattern_points[0].resize(pattern_size.height*pattern_size.width);
	for (int v=0; v<pattern_size.height; ++v)
		for (int u=0; u<pattern_size.width; ++u)
			pattern_points[0][v*pattern_size.width+u] = cv::Point3f(u*chessboard_cell_size, v*chessboard_cell_size, 0.f);
	pattern_points.resize(number_images, pattern_points[0]);
}

void ArmBaseCalibration::extrinsicCalibrationBaseToArm(std::vector< std::vector<cv::Point3f> >& pattern_points_3d,
		std::vector<cv::Mat>& T_base_to_checkerboard_vector, std::vector<cv::Mat>& T_armbase_to_endeff_vector /*, std::vector<cv::Mat>& T_camera_to_marker_vector*/)
{
	// transform 3d chessboard points to respective coordinates systems (base and arm base)
	std::vector<cv::Point3d> points_3d_base, points_3d_armbase;
	for (size_t i=0; i<pattern_points_3d.size(); ++i)
	{
		cv::Mat T_armbase_to_checkerboard = T_armbase_to_endeff_vector[i] * T_endeff_to_checkerboard_;

		for (size_t j=0; j<pattern_points_3d[i].size(); ++j)
		{
			cv::Mat point = cv::Mat(cv::Vec4d(pattern_points_3d[i][j].x, pattern_points_3d[i][j].y, pattern_points_3d[i][j].z, 1.0));

			// to base coordinate system
			cv::Mat point_base = T_base_to_checkerboard_vector[i] * point;
			//std::cout << "point_base: " << pattern_points_3d[i][j].x <<", "<< pattern_points_3d[i][j].y <<", "<< pattern_points_3d[i][j].z << " --> " << point_base.at<double>(0,0) <<", "<< point_base.at<double>(1,0) << ", " << point_base.at<double>(2,0) << std::endl;
			points_3d_base.push_back(cv::Point3d(point_base.at<double>(0), point_base.at<double>(1), point_base.at<double>(2)));

			// to armbase coordinate
			cv::Mat point_armbase = T_armbase_to_checkerboard * point;
			//std::cout << "point_torso_lower: " << pattern_points_3d[i][j].x <<", "<< pattern_points_3d[i][j].y <<", "<< pattern_points_3d[i][j].z << " --> " << point_torso_lower.at<double>(0) <<", "<< point_torso_lower.at<double>(1) << ", " << point_torso_lower.at<double>(2) << std::endl;
			points_3d_armbase.push_back(cv::Point3d(point_armbase.at<double>(0), point_armbase.at<double>(1), point_armbase.at<double>(2)));
		}
	}

	T_base_to_armbase_ = robotino_calibration::computeExtrinsicTransform(points_3d_base, points_3d_armbase);
}

void ArmBaseCalibration::extrinsicCalibrationEndeffToCheckerboard(std::vector< std::vector<cv::Point3f> >& pattern_points_3d,
		std::vector<cv::Mat>& T_base_to_checkerboard_vector, std::vector<cv::Mat>& T_armbase_to_endeff_vector)
{
	cv::Mat T_endeff_to_checkerboard(4, 4, CV_32F);
	T_endeff_to_checkerboard.zeros(4, 4, CV_32F);
	for (size_t i=0; i<pattern_points_3d.size(); ++i)
	{
		T_endeff_to_checkerboard += T_armbase_to_endeff_vector[i].inv() * T_base_to_armbase_.inv() * T_base_to_checkerboard_vector[i];
	}

	T_endeff_to_checkerboard_ = T_endeff_to_checkerboard / (double)pattern_points_3d.size();

	/*
	// transform 3d chessboard points to respective coordinates systems (checkerboard and end effector)
	std::vector<cv::Point3d> points_3d_checker, points_3d_endeff;
	for (size_t i=0; i<pattern_points_3d.size(); ++i)
	{
		cv::Mat T_endeff_to_checkerboard = T_armbase_to_endeff_vector[i].inv() * T_base_to_armbase_.inv() * T_base_to_checkerboard_vector[i];

		for (size_t j=0; j<pattern_points_3d[i].size(); ++j)
		{
			cv::Mat point = cv::Mat(cv::Vec4d(pattern_points_3d[i][j].x, pattern_points_3d[i][j].y, pattern_points_3d[i][j].z, 1.0));

			// to checkerboard coordinate system
			cv::Mat point_checker = point.clone();
			//std::cout << "point_base: " << pattern_points_3d[i][j].x <<", "<< pattern_points_3d[i][j].y <<", "<< pattern_points_3d[i][j].z << " --> " << point_base.at<double>(0,0) <<", "<< point_base.at<double>(1,0) << ", " << point_base.at<double>(2,0) << std::endl;
			points_3d_checker.push_back(cv::Point3d(point_checker.at<double>(0), point_checker.at<double>(1), point_checker.at<double>(2)));

			// to endeff coordinate
			cv::Mat point_endeff = T_endeff_to_checkerboard * point;
			//std::cout << "point_torso_lower: " << pattern_points_3d[i][j].x <<", "<< pattern_points_3d[i][j].y <<", "<< pattern_points_3d[i][j].z << " --> " << point_torso_lower.at<double>(0) <<", "<< point_torso_lower.at<double>(1) << ", " << point_torso_lower.at<double>(2) << std::endl;
			points_3d_endeff.push_back(cv::Point3d(point_endeff.at<double>(0), point_endeff.at<double>(1), point_endeff.at<double>(2)));
		}
	}

	T_endeff_to_checkerboard_ = robotino_calibration::computeExtrinsicTransform(points_3d_checker, points_3d_endeff);
	*/
}

bool ArmBaseCalibration::saveCalibration()
{
	bool success = true;

	// save calibration
	std::string filename = arm_calibration_path_ + "arm_calibration.yml";
	cv::FileStorage fs(filename.c_str(), cv::FileStorage::WRITE);
	if (fs.isOpened() == true)
	{
		fs << "T_base_to_armbase" << T_base_to_armbase_;
		fs << "T_endeff_to_checkerboard" << T_endeff_to_checkerboard_;
	}
	else
	{
		std::cout << "Error: ArmBaseCalibration::saveCalibration: Could not write calibration to file.";
		success = false;
	}
	fs.release();

	return success;
}

bool ArmBaseCalibration::loadCalibration()
{
	bool success = true;

	// load calibration
	std::string filename = arm_calibration_path_ + "camera_calibration.yml";
	cv::FileStorage fs(filename.c_str(), cv::FileStorage::READ);
	if (fs.isOpened() == true)
	{
		fs["T_base_armbase"] >> T_base_to_armbase_;
		fs["T_endeff_to_checkerboard"] >> T_endeff_to_checkerboard_;
	}
	else
	{
		std::cout << "Error: ArmBaseCalibration::loadCalibration: Could not read calibration from file.";
		success = false;
	}
	fs.release();

	calibrated_ = true;

	return success;
}

void ArmBaseCalibration::getCalibration(cv::Mat& T_base_to_armbase, cv::Mat& T_endeff_to_checkerboard)
{
	if (calibrated_ == false && loadCalibration() == false)
	{
		std::cout << "Error: ArmBaseCalibration not calibrated and no calibration data available on disk." << std::endl;
		return;
	}

	T_base_to_armbase = T_base_to_armbase_.clone();
	T_endeff_to_checkerboard = T_endeff_to_checkerboard_.clone();
}

void ArmBaseCalibration::displayAndSaveCalibrationResult(const cv::Mat& T_base_to_arm)
{
	// display calibration parameters
	std::stringstream output;
	output << "\n\n\n----- Replace these parameters in your 'squirrel_robotino/robotino_bringup/robots/xyz_robotino/urdf/properties.urdf.xacro' file -----\n\n";
	cv::Vec3d ypr = robotino_calibration::YPRFromRotationMatrix(T_base_to_arm);
	output << "  <!-- pan_tilt mount positions | handeye calibration | relative to base_link -->\n"
			  << "  <property name=\"arm_base_x\" value=\"" << T_base_to_arm.at<double>(0,3) << "\"/>\n"
			  << "  <property name=\"arm_base_y\" value=\"" << T_base_to_arm.at<double>(1,3) << "\"/>\n"
			  << "  <property name=\"arm_base_z\" value=\"" << T_base_to_arm.at<double>(2,3) << "\"/>\n"
			  << "  <property name=\"arm_base_roll\" value=\"" << ypr.val[2] << "\"/>\n"
			  << "  <property name=\"arm_base_pitch\" value=\"" << ypr.val[1] << "\"/>\n"
			  << "  <property name=\"arm_base_yaw\" value=\"" << ypr.val[0] << "\"/>\n\n";
	std::cout << output.str();

	std::string path_file = arm_calibration_path_ + "arm_calibration_urdf.txt";
	std::fstream file_output;
	file_output.open(path_file.c_str(), std::ios::out);
	if (file_output.is_open())
		file_output << output.str();
	file_output.close();
}

