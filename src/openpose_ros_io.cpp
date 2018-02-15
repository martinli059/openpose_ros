#include <openpose_ros_io.h>

using namespace openpose_ros;

OpenPoseROSIO::OpenPoseROSIO(): it_(nh_)
{
    // Subscribe to input video feed and publish human lists as output
    std::string image_topic;
    std::string output_topic;

    nh_.param("image_topic", image_topic, "/camera/image_raw");
    nh_.param("output_topic", output_topic, "/openpose_ros/human_list");

    image_sub_ = it_.subscribe(image_topic, 1, &OpenPoseROSIO::convertImage, this);
    openpose_human_list_pub_ = nh_.advertise<openpose_ros::OpenPoseHumanList>(output_topic, 10);
    cv_img_ptr_ = nullptr;
}

void OpenPoseROSIO::convertImage(const sensor_msgs::ImageConstPtr& msg)
{
    try
    {
        cv_img_ptr_ = cv_bridge::toCvCopy(msg, sensor_msgs::image_encodings::BGR8);
        rgb_image_header_ = msg->header;
    }
    catch (cv_bridge::Exception& e)
    {
        ROS_ERROR("cv_bridge exception: %s", e.what());
        return;
    }
}

std::shared_ptr<std::vector<op::Datum>> OpenPoseROSIO::createDatum()
{
    // Close program when empty frame
    if (cv_img_ptr_ == nullptr)
    {
        return nullptr;
    }
    else // if (cv_img_ptr_ == nullptr)
    {
        // Create new datum
        auto datumsPtr = std::make_shared<std::vector<op::Datum>>();
        datumsPtr->emplace_back();
        auto& datum = datumsPtr->at(0);

        // Fill datum
        datum.cvInputData = cv_img_ptr_->image;

        return datumsPtr;
    }
}

bool OpenPoseROSIO::display(const std::shared_ptr<std::vector<op::Datum>>& datumsPtr)
{
    // User's displaying/saving/other processing here
        // datum.cvOutputData: rendered frame with pose or heatmaps
        // datum.poseKeypoints: Array<float> with the estimated pose
    char key = ' ';
    if (datumsPtr != nullptr && !datumsPtr->empty())
    {
        cv::imshow("User worker GUI", datumsPtr->at(0).cvOutputData);
        // Display image and sleeps at least 1 ms (it usually sleeps ~5-10 msec to display the image)
        key = (char)cv::waitKey(1);
    }
    else
        op::log("Nullptr or empty datumsPtr found.", op::Priority::High, __LINE__, __FUNCTION__, __FILE__);
    return (key == 27);
}

cv_bridge::CvImagePtr& OpenPoseROSIO::getCvImagePtr()
{
    return cv_img_ptr_;
}

void OpenPoseROSIO::printKeypoints(const std::shared_ptr<std::vector<op::Datum>>& datumsPtr)
{
    // Example: How to use the pose keypoints
    if (datumsPtr != nullptr && !datumsPtr->empty())
    {
        op::log("\nKeypoints:");
        // Accesing each element of the keypoints
        const auto& poseKeypoints = datumsPtr->at(0).poseKeypoints;
        op::log("Person pose keypoints:");
        for (auto person = 0 ; person < poseKeypoints.getSize(0) ; person++)
        {
            op::log("Person " + std::to_string(person) + " (x, y, score):");
            for (auto bodyPart = 0 ; bodyPart < poseKeypoints.getSize(1) ; bodyPart++)
            {
                std::string valueToPrint;
                for (auto xyscore = 0 ; xyscore < poseKeypoints.getSize(2) ; xyscore++)
                    valueToPrint += std::to_string(   poseKeypoints[{person, bodyPart, xyscore}]   ) + " ";
                op::log(valueToPrint);
            }
        }
        op::log(" ");
        // Alternative: just getting std::string equivalent
        op::log("Face keypoints: " + datumsPtr->at(0).faceKeypoints.toString());
        op::log("Left hand keypoints: " + datumsPtr->at(0).handKeypoints[0].toString());
        op::log("Right hand keypoints: " + datumsPtr->at(0).handKeypoints[1].toString());
        // Heatmaps
        const auto& poseHeatMaps = datumsPtr->at(0).poseHeatMaps;
        if (!poseHeatMaps.empty())
        {
            op::log("Pose heatmaps size: [" + std::to_string(poseHeatMaps.getSize(0)) + ", "
                    + std::to_string(poseHeatMaps.getSize(1)) + ", "
                    + std::to_string(poseHeatMaps.getSize(2)) + "]");
            const auto& faceHeatMaps = datumsPtr->at(0).faceHeatMaps;
            op::log("Face heatmaps size: [" + std::to_string(faceHeatMaps.getSize(0)) + ", "
                    + std::to_string(faceHeatMaps.getSize(1)) + ", "
                    + std::to_string(faceHeatMaps.getSize(2)) + ", "
                    + std::to_string(faceHeatMaps.getSize(3)) + "]");
            const auto& handHeatMaps = datumsPtr->at(0).handHeatMaps;
            op::log("Left hand heatmaps size: [" + std::to_string(handHeatMaps[0].getSize(0)) + ", "
                    + std::to_string(handHeatMaps[0].getSize(1)) + ", "
                    + std::to_string(handHeatMaps[0].getSize(2)) + ", "
                    + std::to_string(handHeatMaps[0].getSize(3)) + "]");
            op::log("Right hand heatmaps size: [" + std::to_string(handHeatMaps[1].getSize(0)) + ", "
                    + std::to_string(handHeatMaps[1].getSize(1)) + ", "
                    + std::to_string(handHeatMaps[1].getSize(2)) + ", "
                    + std::to_string(handHeatMaps[1].getSize(3)) + "]");
        }
    }
    else
        op::log("Nullptr or empty datumsPtr found.", op::Priority::High, __LINE__, __FUNCTION__, __FILE__);
}

void OpenPoseROSIO::publish(const std::shared_ptr<std::vector<op::Datum>>& datumsPtr)
{
    if (datumsPtr != nullptr && !datumsPtr->empty() && !FLAGS_body_disable)
    {
        const auto& poseKeypoints = datumsPtr->at(0).poseKeypoints;
        const auto& faceKeypoints = datumsPtr->at(0).faceKeypoints;
        const auto& leftHandKeypoints = datumsPtr->at(0).handKeypoints[0];
        const auto& rightHandKeypoints = datumsPtr->at(0).handKeypoints[1];
        std::vector<op::Rectangle<float>>& face_rectangles = datumsPtr->at(0).faceRectangles;

        openpose_ros::OpenPoseHumanList human_list_msg;
        human_list_msg.header.stamp = ros::Time::now();
        human_list_msg.rgb_image_header = rgb_image_header_;
        human_list_msg.num_humans = poseKeypoints.getSize(0);
        
        std::vector<openpose_ros::OpenPoseHuman> human_list(poseKeypoints.getSize(0));

        for (auto person = 0 ; person < poseKeypoints.getSize(0) ; person++)
        {
            openpose_ros::OpenPoseHuman human;

            int num_body_key_points_with_non_zero_prob = 0;
            std::vector<openpose_ros::PointWithProb> body_key_points_with_prob(poseKeypoints.getSize(1));

            for (auto bodyPart = 0 ; bodyPart < poseKeypoints.getSize(1) ; bodyPart++)
            {
                openpose_ros::PointWithProb body_point_with_prob;
                body_point_with_prob.x = poseKeypoints[{person, bodyPart, 0}];
                body_point_with_prob.y = poseKeypoints[{person, bodyPart, 1}];
                body_point_with_prob.prob = poseKeypoints[{person, bodyPart, 2}];
                if(body_point_with_prob.prob > 0)
                {
                    num_body_key_points_with_non_zero_prob++;
                }
                body_key_points_with_prob.at(bodyPart) = body_point_with_prob;
            }
            human.num_body_key_points_with_non_zero_prob = num_body_key_points_with_non_zero_prob;
            human.body_key_points_with_prob = body_key_points_with_prob;

            if(FLAGS_face)
            {
                int num_face_key_points_with_non_zero_prob = 0;
                std::vector<openpose_ros::PointWithProb> face_key_points_with_prob(faceKeypoints.getSize(1));

                for (auto facePart = 0 ; facePart < faceKeypoints.getSize(1) ; facePart++)
                {
                    openpose_ros::PointWithProb face_point_with_prob;
                    face_point_with_prob.x = faceKeypoints[{person, facePart, 0}];
                    face_point_with_prob.y = faceKeypoints[{person, facePart, 1}];
                    face_point_with_prob.prob = faceKeypoints[{person, facePart, 2}];
                    if(face_point_with_prob.prob > 0)
                    {
                        num_face_key_points_with_non_zero_prob++;
                    }
                    face_key_points_with_prob.at(facePart) = face_point_with_prob;
                }  
                human.num_face_key_points_with_non_zero_prob = num_face_key_points_with_non_zero_prob;
                human.face_key_points_with_prob = face_key_points_with_prob;

                openpose_ros::BoundingBox face_bounding_box;
                face_bounding_box.x = face_rectangles.at(person).x;
                face_bounding_box.y = face_rectangles.at(person).y;
                face_bounding_box.width = face_rectangles.at(person).width;
                face_bounding_box.height = face_rectangles.at(person).height;
                human.face_bounding_box = face_bounding_box;
            }
            
            if(FLAGS_hand)
            {

                int num_right_hand_key_points_with_non_zero_prob = 0;
                int num_left_hand_key_points_with_non_zero_prob = 0;
                std::vector<openpose_ros::PointWithProb> right_hand_key_points_with_prob(rightHandKeypoints.getSize(1));
                std::vector<openpose_ros::PointWithProb> left_hand_key_points_with_prob(leftHandKeypoints.getSize(1));

                for (auto handPart = 0 ; handPart < rightHandKeypoints.getSize(1) ; handPart++)
                {
                    openpose_ros::PointWithProb right_hand_point_with_prob;
                    openpose_ros::PointWithProb left_hand_point_with_prob;
                    right_hand_point_with_prob.x = rightHandKeypoints[{person, handPart, 0}];
                    right_hand_point_with_prob.y = rightHandKeypoints[{person, handPart, 1}];
                    right_hand_point_with_prob.prob = rightHandKeypoints[{person, handPart, 2}];
                    if(right_hand_point_with_prob.prob > 0)
                    {
                        num_right_hand_key_points_with_non_zero_prob++;
                    }
                    left_hand_point_with_prob.x = leftHandKeypoints[{person, handPart, 0}];
                    left_hand_point_with_prob.y = leftHandKeypoints[{person, handPart, 1}];
                    left_hand_point_with_prob.prob = leftHandKeypoints[{person, handPart, 2}];
                    if(left_hand_point_with_prob.prob > 0)
                    {
                        num_left_hand_key_points_with_non_zero_prob++;
                    }
                    right_hand_key_points_with_prob.at(handPart) = right_hand_point_with_prob;
                    left_hand_key_points_with_prob.at(handPart) = left_hand_point_with_prob;
                }
                human.num_right_hand_key_points_with_non_zero_prob = num_right_hand_key_points_with_non_zero_prob;
                human.num_left_hand_key_points_with_non_zero_prob = num_left_hand_key_points_with_non_zero_prob;
                human.right_hand_key_points_with_prob = right_hand_key_points_with_prob;
                human.left_hand_key_points_with_prob = left_hand_key_points_with_prob;
            }

            human_list.at(person) = human;
        }

        human_list_msg.human_list = human_list;

        openpose_human_list_pub_.publish(human_list_msg);

    }
    else
        op::log("Nullptr or empty datumsPtr found.", op::Priority::High, __LINE__, __FUNCTION__, __FILE__);
}