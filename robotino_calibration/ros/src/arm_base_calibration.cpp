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
#include <std_msgs/Float64MultiArray.h>
#include <geometry_msgs/Point.h>
#include <pcl/point_types.h>
#include <pcl/registration/icp.h>
#include <sstream>
#include <fstream>
#include <numeric>
#include <robotino_calibration/timer.h>


//ToDo: Adjust displayAndSaveCalibrationResult() for new EndeffToChecker or remove the optimization for it.
//ToDo: Add new setting to define the timeout time (move camera, move robot)
//ToDo: Rename armbase_to_endeff to armbase_to_checkerboard

ArmBaseCalibration::ArmBaseCalibration(ros::NodeHandle nh) :
		RobotCalibration(nh, true), camera_dof_(2)
{
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
	node_handle_.param("arm_dof", arm_dof_, 5);
	std::cout << "arm_dof: " << arm_dof_ << std::endl;

	if ( arm_dof_ < 1 )
	{
		std::cout << "Error: Invalid arm_dof: " << arm_dof_ << ". Setting arm_dof to 1." << std::endl;
		arm_dof_ = 1;
	}

	node_handle_.param("camera_dof", camera_dof_, 5);
	std::cout << "camera_dof: " << camera_dof_ << std::endl;

	if ( camera_dof_ < 1 )
	{
		std::cout << "Error: Invalid camera_dof: " << camera_dof_ << ". Setting camera_dof to 1." << std::endl;
		camera_dof_ = 1;
	}

	// coordinate frame name parameters
	node_handle_.param<std::string>("armbase_frame", armbase_frame_, "");
	std::cout << "armbase_frame: " << armbase_frame_ << std::endl;
	node_handle_.param<std::string>("checkerboard_frame", checkerboard_frame_, "");
	std::cout << "checkerboard_frame: " << checkerboard_frame_ << std::endl;
	node_handle_.param<std::string>("camera_optical_frame", camera_optical_frame_, "kinect_rgb_optical_frame");
	std::cout << "camera_optical_frame: " << camera_optical_frame_ << std::endl;
	node_handle_.param<std::string>("camera_image_topic", camera_image_topic_, "/kinect/rgb/image_raw");
	std::cout << "camera_image_topic: " << camera_image_topic_ << std::endl;
	node_handle_.param("max_angle_deviation", max_angle_deviation_, 0.5);
	std::cout << "max_angle_deviation: " << max_angle_deviation_ << std::endl;

	// initial parameters
	bool success = transform_utilities::getTransform(transform_listener_, base_frame_, armbase_frame_, T_base_to_armbase_);
	if ( success == false )
	{
		ROS_FATAL("Could not retrieve transform from %s to %s from TF!",base_frame_.c_str(),armbase_frame_.c_str());
		throw std::exception();
	}
	else
		std::cout << "T_base_to_armbase_initial:\n" << T_base_to_armbase_ << std::endl;

	/*temp.clear();
	T_base_to_armbase_ = transform_utilities::makeTransform(transform_utilities::rotationMatrixFromYPR(0.0, 0.0, 0.0), cv::Mat(cv::Vec3d(0.0, 0.0, 0.0)));
	node_handle_.getParam("T_base_to_armbase_initial", temp);
	if ( temp.size()==6 )
		T_base_to_armbase_ = transform_utilities::makeTransform(transform_utilities::rotationMatrixFromYPR(temp[3], temp[4], temp[5]), cv::Mat(cv::Vec3d(temp[0], temp[1], temp[2])));
	std::cout << "T_base_to_arm_initial:\n" << T_base_to_armbase_ << std::endl;
	temp.clear();*/

	// read out user-defined end effector configurations
	temp.clear();
	node_handle_.getParam("arm_configurations", temp);
	const int number_configurations = temp.size()/arm_dof_;

	if (temp.size()%arm_dof_ != 0 || temp.size() < 3*arm_dof_)
	{
		ROS_ERROR("The arm_configurations vector should contain at least 3 configurations with %d values each.", arm_dof_);
		return;
	}
	std::cout << "arm configurations:\n";
	for ( int i=0; i<number_configurations; ++i )
	{
		std::vector<double> angles;
		for ( int j=0; j<arm_dof_; ++j )
		{
			angles.push_back(temp[arm_dof_*i + j]);
			std::cout << angles[angles.size()-1] << "\t";
		}
		std::cout << std::endl;

		arm_configurations_.push_back(calibration_utilities::AngleConfiguration(angles));
	}

	temp.clear();
	node_handle_.getParam("camera_configurations", temp);
	double cam_configs = temp.size()/camera_dof_;
	if ( cam_configs != number_configurations )
	{
		ROS_ERROR("The camera_configurations vector must hold as many configurations as the arm_configurations vector.");
		return;
	}
	std::cout << "camera configurations:\n";
	for ( int i=0; i<cam_configs; ++i )
	{
		std::vector<double> angles;
		for ( int j=0; j<camera_dof_; ++j )
		{
			angles.push_back(temp[camera_dof_*i + j]);
			std::cout << angles[angles.size()-1] << "\t";
		}
		std::cout << std::endl;

		camera_configurations_.push_back(calibration_utilities::AngleConfiguration(angles));
	}

	// set up messages
	it_ = new image_transport::ImageTransport(node_handle_);
	color_image_sub_.subscribe(*it_, camera_image_topic_, 1);
	color_image_sub_.registerCallback(boost::bind(&ArmBaseCalibration::imageCallback, this, _1));

	ROS_INFO("ArmBaseCalibration initialized.");
}

ArmBaseCalibration::~ArmBaseCalibration()
{
	if (it_ != 0)
		delete it_;
}

void ArmBaseCalibration::imageCallback(const sensor_msgs::ImageConstPtr& color_image_msg)
{
	// secure this access with a mutex
	boost::mutex::scoped_lock lock(camera_data_mutex_);

	if (capture_image_ == true)
	{
		// read image
		cv_bridge::CvImageConstPtr color_image_ptr;
		if (calibration_utilities::convertImageMessageToMat(color_image_msg, color_image_ptr, camera_image_) == false)
			return;

		latest_image_time_ = color_image_msg->header.stamp;

		if ( latest_image_time_.toSec() <= 0.01f ) //No time stamps -> use current time as stamp
			latest_image_time_ = ros::Time::now();

		capture_image_ = false;
	}
}

bool ArmBaseCalibration::calibrateArmToBase(const bool load_images)
{
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
	std::vector<cv::Mat> T_armbase_to_checkerboard_vector;
	std::vector<cv::Mat> T_base_to_camera_optical_vector;
	acquireCalibrationImages(chessboard_pattern_size_, load_images, image_width, image_height, points_2d_per_image, T_armbase_to_checkerboard_vector,
			T_base_to_camera_optical_vector);

	if ( points_2d_per_image.size() == 0 )
	{
		ROS_WARN("Skipping calibration, as no calibration images are available.");
		return false;
	}

	// prepare chessboard 3d points
	std::vector< std::vector<cv::Point3f> > pattern_points_3d;
	calibration_utilities::computeCheckerboard3dPoints(pattern_points_3d, chessboard_pattern_size_, chessboard_cell_size_, points_2d_per_image.size());

	// intrinsic calibration for camera, get camera to checkerboard vector
	std::vector<cv::Mat> rvecs, tvecs;
	std::vector<cv::Mat> T_base_to_checkerboard_vector;
	intrinsicCalibration(pattern_points_3d, points_2d_per_image, cv::Size(image_width, image_height), rvecs, tvecs);
	for (size_t i=0; i<rvecs.size(); ++i)
	{
		cv::Mat R, t;
		cv::Rodrigues(rvecs[i], R);
		cv::Mat T_base_to_checkerboard = T_base_to_camera_optical_vector[i] * transform_utilities::makeTransform(R, tvecs[i]);
		T_base_to_checkerboard_vector.push_back(T_base_to_checkerboard);
	}

	// extrinsic calibration between base and arm_base
	extrinsicCalibrationBaseToArm(pattern_points_3d, T_base_to_checkerboard_vector, T_armbase_to_checkerboard_vector );

	// display calibration parameters
	displayAndSaveCalibrationResult(T_base_to_armbase_);

	// save calibration
	saveCalibration();
	calibrated_ = true;
	return true;
}

void ArmBaseCalibration::intrinsicCalibration(const std::vector< std::vector<cv::Point3f> >& pattern_points, const std::vector< std::vector<cv::Point2f> >& camera_points_2d_per_image, const cv::Size& image_size, std::vector<cv::Mat>& rvecs, std::vector<cv::Mat>& tvecs)
{
	std::cout << "Intrinsic calibration started ..." << std::endl;
	K_ = cv::Mat::eye(3, 3, CV_64F);
	distortion_ = cv::Mat::zeros(8, 1, CV_64F);
	cv::calibrateCamera(pattern_points, camera_points_2d_per_image, image_size, K_, distortion_, rvecs, tvecs);
	std::cout << "Intrinsic calibration:\nK:\n" << K_ << "\ndistortion:\n" << distortion_ << std::endl;

	double error = computeReprojectionErrors(pattern_points, camera_points_2d_per_image, rvecs, tvecs, K_, distortion_);
	std::cout << "Total reprojection error: " << error << std::endl;
}

double ArmBaseCalibration::computeReprojectionErrors( const std::vector<std::vector<cv::Point3f> >& objectPoints,
                                         const std::vector<std::vector<cv::Point2f> >& imagePoints,
                                         const std::vector<cv::Mat>& rvecs, const std::vector<cv::Mat>& tvecs,
                                         const cv::Mat& cameraMatrix , const cv::Mat& distCoeffs)
{
    std::vector<cv::Point2f> imagePoints2;
    size_t totalPoints = 0;
    double totalErr = 0, err = 0;

    for(size_t i = 0; i < objectPoints.size(); ++i )
    {
        cv::projectPoints(objectPoints[i], rvecs[i], tvecs[i], cameraMatrix, distCoeffs, imagePoints2);

        err = cv::norm(imagePoints[i], imagePoints2, cv::NORM_L2);
        size_t n = objectPoints[i].size();
        //double perViewError = (float) std::sqrt(err*err/n);
        //std::cout << "View error " << (i+1) << ": " << perViewError << std::endl;
        totalErr        += err*err;
        totalPoints     += n;
    }
    return std::sqrt(totalErr/totalPoints);
}

bool ArmBaseCalibration::moveArm(const calibration_utilities::AngleConfiguration& arm_configuration)
{
	std_msgs::Float64MultiArray new_joint_config;
	new_joint_config.data.resize(arm_configuration.angles_.size());

	for ( int i=0; i<new_joint_config.data.size(); ++i )
		new_joint_config.data[i] = arm_configuration.angles_[i];

	std::vector<double> cur_state = *calibration_interface_->getCurrentArmState();
	if ( cur_state.size() != arm_configuration.angles_.size() )
	{
		ROS_ERROR("Size of target arm configuration and count of arm joints do not match! Please adjust the yaml file.");
		return false;
	}

	// Ensure that target angles and current angles are not too far away from one another to avoid collision issues!
	for ( size_t i=0; i<cur_state.size(); ++i )
	{
		double delta_angle = 0.0;

		do
		{
			cur_state = *calibration_interface_->getCurrentArmState();
			delta_angle = arm_configuration.angles_[i] - cur_state[i];

			while (delta_angle < -CV_PI)
				delta_angle += 2*CV_PI;
			while (delta_angle > CV_PI)
				delta_angle -= 2*CV_PI;

			if ( abs(delta_angle) > max_angle_deviation_ )
			{
				ROS_WARN("%d. target angle exceeds max allowed deviation of %f!\n"
						"Please move the arm manually closer to the target position to avoid collision issues. Waiting 5 seconds...", (int)(i+1), max_angle_deviation_);
				std::cout << "Current arm state: ";
				for ( size_t j=0; j<cur_state.size(); ++j )
					std::cout << cur_state[j] << (j<cur_state.size()-1 ? "\t" : "\n");
				std::cout << "Target arm state: ";
				for ( size_t j=0; j<cur_state.size(); ++j )
					std::cout << arm_configuration.angles_[j] << (j<cur_state.size()-1 ? "\t" : "\n");
				ros::Duration(5).sleep();
				ros::spinOnce();
			}
		} while ( abs(delta_angle) > max_angle_deviation_ && ros::ok() );
	}

	//arm_joint_controller_.publish(new_joint_config);
	calibration_interface_->assignNewArmJoints(new_joint_config);

	//Wait for arm to move
	if ( (*calibration_interface_->getCurrentArmState()).size() > 0 )
	{
		Timer timeout;
		while (timeout.getElapsedTimeInSec()<10.0) //Max. 10 seconds to reach goal
		{

			boost::mutex::scoped_lock(arm_state_data_mutex_);
			cur_state = *calibration_interface_->getCurrentArmState();
			std::vector<double> difference(cur_state.size());
			for (int i = 0; i<cur_state.size(); ++i)
				difference[i] = arm_configuration.angles_[i]-cur_state[i];

			double length = std::sqrt(std::inner_product(difference.begin(), difference.end(), difference.begin(), 0.0)); //Length of difference vector in joint space

			if ( length < 0.025 ) //Close enough to goal configuration (~1° deviation allowed)
			{
				std::cout << "Arm configuration reached, deviation: " << length << std::endl;
				break;
			}

			ros::spinOnce();
		}

		if ( timeout.getElapsedTimeInSec()>=10.0 )
		{
			ROS_WARN("Could not reach following arm configuration in time:");
			for (int i = 0; i<arm_configuration.angles_.size(); ++i)
				std::cout << arm_configuration.angles_[i] << "\t";
			std::cout << std::endl;
		}
	}
	else
		ros::Duration(1).sleep();

	ros::spinOnce();
	return true;
}

bool ArmBaseCalibration::moveCamera(const calibration_utilities::AngleConfiguration& cam_configuration)
{
	std_msgs::Float64MultiArray angles;
	angles.data.resize(cam_configuration.angles_.size());

	for ( int i=0; i<angles.data.size(); ++i )
		angles.data[i] = cam_configuration.angles_[i];

	std::vector<double> cur_state = *calibration_interface_->getCurrentCameraState();
	if ( cur_state.size() != cam_configuration.angles_.size() )
	{
		ROS_ERROR("Size of target camera configuration and count of camera joints do not match! Please adjust the yaml file.");
		return false;
	}

	calibration_interface_->assignNewCameraAngles(angles);

	// wait for pan tilt to arrive at goal position
	if ( cur_state.size() > 0 )
	{
		Timer timeout;
		while (timeout.getElapsedTimeInSec()<10.0)
		{
			cur_state = *calibration_interface_->getCurrentCameraState();
			std::vector<double> difference(cur_state.size());
			for (int i = 0; i<cur_state.size(); ++i)
				difference[i] = cam_configuration.angles_[i]-cur_state[i];

			double length = std::sqrt(std::inner_product(difference.begin(), difference.end(), difference.begin(), 0.0)); //Length of difference vector in joint space

			if ( length < 0.02 ) //Close enough to goal configuration (~0.5° deviation allowed)
			{
				std::cout << "Camera configuration reached, deviation: " << length << std::endl;
				break;
			}

			ros::spinOnce();
		}

		if ( timeout.getElapsedTimeInSec()>=10.0 )
		{
			ROS_WARN("Could not reach following camera configuration in time:");
			for (int i = 0; i<cam_configuration.angles_.size(); ++i)
				std::cout << cam_configuration.angles_[i] << "\t";
			std::cout << std::endl;
		}
	}
	else
	{
		ros::Duration(1).sleep();
	}

	ros::spinOnce();
	return true;
}

bool ArmBaseCalibration::acquireCalibrationImages(const cv::Size pattern_size, const bool load_images, int& image_width, int& image_height,
		std::vector< std::vector<cv::Point2f> >& points_2d_per_image, std::vector<cv::Mat>& T_armbase_to_checkerboard_vector,
		std::vector<cv::Mat>& T_base_to_camera_optical_vector)
{
	// capture images from different perspectives
	const int number_images_to_capture = (int)arm_configurations_.size();
	for (int image_counter = 0; image_counter < number_images_to_capture; ++image_counter)
	{
		if ( !ros::ok() )
			return false;

		std::cout << "Configuration " << (image_counter+1) << "/" << number_images_to_capture << std::endl;

		if (!load_images)
		{
			moveCamera(camera_configurations_[image_counter]);
			moveArm(arm_configurations_[image_counter]);
		}

		// acquire image and extract checkerboard points
		std::vector<cv::Point2f> checkerboard_points_2d;
		int return_value = acquireCalibrationImage(image_width, image_height, checkerboard_points_2d, pattern_size, load_images, image_counter);
		if (return_value != 0)
			continue;

		// retrieve transformations
		cv::Mat T_armbase_to_checkerboard, T_base_to_camera_optical;
		std::stringstream path;
		path << calibration_storage_path_ << image_counter << ".yml";
		if (load_images)
		{
			cv::FileStorage fs(path.str().c_str(), cv::FileStorage::READ);
			if (fs.isOpened())
			{
				fs["T_armbase_to_checkerboard"] >> T_armbase_to_checkerboard;
				fs["T_base_to_camera_optical"] >> T_base_to_camera_optical;
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
			result &= transform_utilities::getTransform(transform_listener_, armbase_frame_, checkerboard_frame_, T_armbase_to_checkerboard);
			result &= transform_utilities::getTransform(transform_listener_, base_frame_, camera_optical_frame_, T_base_to_camera_optical);

			if (result == false)
				continue;

			// save transforms to file
			cv::FileStorage fs(path.str().c_str(), cv::FileStorage::WRITE);
			if (fs.isOpened())
			{
				fs << "T_armbase_to_checkerboard" << T_armbase_to_checkerboard;
				fs << "T_base_to_camera_optical" << T_base_to_camera_optical;
			}
			else
			{
				ROS_WARN("Could not write transformations to file '%s'.", path.str().c_str());
				continue;
			}
			fs.release();
		}

		points_2d_per_image.push_back(checkerboard_points_2d);
		T_armbase_to_checkerboard_vector.push_back(T_armbase_to_checkerboard);
		T_base_to_camera_optical_vector.push_back(T_base_to_camera_optical);
		std::cout << "Captured perspectives: " << points_2d_per_image.size() << std::endl;
	}

	return true;
}

int ArmBaseCalibration::acquireCalibrationImage(int& image_width, int& image_height,
		std::vector<cv::Point2f>& checkerboard_points_2d, const cv::Size pattern_size, const bool load_images, int& image_counter)
{
	int return_value = 0;

	// acquire image
	cv::Mat image, gray;
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

				// convert to grayscale
				gray = cv::Mat::zeros(image.rows, image.cols, CV_8UC1);
				cv::cvtColor(image, gray, CV_BGR2GRAY);
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
		ss << calibration_storage_path_ << image_counter;
		std::string image_name = ss.str() + ".png";
		gray = cv::imread(image_name.c_str(), CV_LOAD_IMAGE_GRAYSCALE);
		if (gray.empty())
			return -2;
	}
	image_width = gray.cols;
	image_height = gray.rows;

	// find pattern in image
	bool pattern_found = cv::findChessboardCorners(gray, pattern_size, checkerboard_points_2d,
								cv::CALIB_CB_ADAPTIVE_THRESH + cv::CALIB_CB_NORMALIZE_IMAGE + cv::CALIB_CB_FAST_CHECK);

	if ( pattern_found )
	{
        cv::cornerSubPix( gray, checkerboard_points_2d, cv::Size(11,11),
        		cv::Size(-1,-1), cv::TermCriteria( cv::TermCriteria::EPS+cv::TermCriteria::COUNT, 30, 0.1 ));
	}

	// collect 2d points
	if (checkerboard_points_2d.size() == pattern_size.height*pattern_size.width)
	{
		// save images
		if (load_images == false)
		{
			std::stringstream ss;
			ss << calibration_storage_path_ << image_counter;
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

void ArmBaseCalibration::extrinsicCalibrationBaseToArm(std::vector< std::vector<cv::Point3f> >& pattern_points_3d,
		std::vector<cv::Mat>& T_base_to_checkerboard_vector, std::vector<cv::Mat>& T_armbase_to_checkerboard_vector )
{
	// transform 3d chessboard points to respective coordinates systems (base and arm base)
	std::vector<cv::Point3d> points_3d_base, points_3d_armbase;
	for (size_t i=0; i<pattern_points_3d.size(); ++i)
	{
		for (size_t j=0; j<pattern_points_3d[i].size(); ++j)
		{
			cv::Mat point = cv::Mat(cv::Vec4d(pattern_points_3d[i][j].x, pattern_points_3d[i][j].y, pattern_points_3d[i][j].z, 1.0));

			// to base coordinate system
			cv::Mat point_base = T_base_to_checkerboard_vector[i] * point; // from base to camera to checkerboard corners
			points_3d_base.push_back(cv::Point3d(point_base.at<double>(0), point_base.at<double>(1), point_base.at<double>(2)));

			// to armbase coordinate
			cv::Mat point_armbase = T_armbase_to_checkerboard_vector[i] * point; // from arm base to checkerboard corners
			points_3d_armbase.push_back(cv::Point3d(point_armbase.at<double>(0), point_armbase.at<double>(1), point_armbase.at<double>(2)));
		}
	}

	T_base_to_armbase_ = transform_utilities::computeExtrinsicTransform(points_3d_base, points_3d_armbase);
}

bool ArmBaseCalibration::saveCalibration()
{
	bool success = true;

	// save calibration
	std::string filename = calibration_storage_path_ + "arm_calibration_result.yml";
	cv::FileStorage fs(filename.c_str(), cv::FileStorage::WRITE);
	if (fs.isOpened() == true)
	{
		fs << "T_base_to_armbase" << T_base_to_armbase_;
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
	std::string filename = calibration_storage_path_ + "arm_calibration_result.yml";
	cv::FileStorage fs(filename.c_str(), cv::FileStorage::READ);
	if (fs.isOpened() == true)
	{
		fs["T_base_armbase"] >> T_base_to_armbase_;
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

void ArmBaseCalibration::getCalibration(cv::Mat& T_base_to_armbase)
{
	if (calibrated_ == false && loadCalibration() == false)
	{
		std::cout << "Error: ArmBaseCalibration not calibrated and no calibration data available on disk." << std::endl;
		return;
	}

	T_base_to_armbase = T_base_to_armbase_.clone();
}

void ArmBaseCalibration::displayAndSaveCalibrationResult(const cv::Mat& T_base_to_arm)
{
	// display calibration parameters
	std::stringstream output;
	output << "\n\n\n----- Replace these parameters in your 'squirrel_robotino/robotino_bringup/robots/xyz_robotino/urdf/properties.urdf.xacro' file -----\n\n";
	cv::Vec3d ypr = transform_utilities::YPRFromRotationMatrix(T_base_to_arm);
	output << "  <!-- arm mount positions | handeye calibration | relative to base_link -->\n"
			  << "  <property name=\"arm_base_x\" value=\"" << T_base_to_arm.at<double>(0,3) << "\"/>\n"
			  << "  <property name=\"arm_base_y\" value=\"" << T_base_to_arm.at<double>(1,3) << "\"/>\n"
			  << "  <property name=\"arm_base_z\" value=\"" << T_base_to_arm.at<double>(2,3) << "\"/>\n"
			  << "  <property name=\"arm_base_roll\" value=\"" << ypr.val[2] << "\"/>\n"
			  << "  <property name=\"arm_base_pitch\" value=\"" << ypr.val[1] << "\"/>\n"
			  << "  <property name=\"arm_base_yaw\" value=\"" << ypr.val[0] << "\"/>\n\n";
	std::cout << output.str();

	if ( ros::ok() )
	{
		std::string path_file = calibration_storage_path_ + "arm_calibration_urdf.txt";
		std::fstream file_output;
		file_output.open(path_file.c_str(), std::ios::out);
		if (file_output.is_open())
			file_output << output.str();
		file_output.close();
	}
	else
	{
		ROS_INFO("Skipping to save calibration result.");
	}
}

