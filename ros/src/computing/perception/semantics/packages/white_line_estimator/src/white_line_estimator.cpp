//headers in this package
#include <white_line_estimator/white_line_estimator.h>

WhiteLineEstimator::WhiteLineEstimator(ros::NodeHandle nh,ros::NodeHandle pnh) : it_(nh)
{
    nh_ = nh;
    pnh_ = pnh;
    pointcloud_projector_ptr_ = boost::make_shared<PointCloudProjector>(500,15000);
    image_points_projector_ptr_ = boost::make_shared<ImagePointsProjector>();
    marker_pub_ = pnh_.advertise<visualization_msgs::MarkerArray>("marker",1);
    image_pub_ = it_.advertise("/ground_image", 10);
    image_sub_ptr_ = boost::make_shared<message_filters::Subscriber<sensor_msgs::Image> >(nh_, "image_raw", 1);
    pointcloud_sub_ptr_ = boost::make_shared<message_filters::Subscriber<sensor_msgs::PointCloud2> >(nh_, "points_ground", 1);
    sync_ptr_ = boost::make_shared<message_filters::Synchronizer<SyncPolicy> >(SyncPolicy(10), *image_sub_ptr_, *pointcloud_sub_ptr_);
    sync_ptr_->registerCallback(boost::bind(&WhiteLineEstimator::sensorCallback,this,_1,_2));
    camera_info_sub_ = nh_.subscribe("camera_info",1,&WhiteLineEstimator::cameraInfoCallback,this);
    projection_matrix_sub_ = nh_.subscribe("projection_matrix",1,&WhiteLineEstimator::projectionMatrixCallback,this);
    callback_func_type_ = boost::bind(&WhiteLineEstimator::configureCallback, this, _1, _2);
    server_.setCallback(callback_func_type_);
}

WhiteLineEstimator::~WhiteLineEstimator()
{

}

visualization_msgs::MarkerArray WhiteLineEstimator::generateMarkers(std::vector<std::vector<geometry_msgs::Point> > points,std::string frame, ros::Time stamp)
{
    visualization_msgs::MarkerArray markers;
    for(int i=0; i<points.size(); i++)
    {
        for(int m=0; m<points[i].size(); m++)
        {
            visualization_msgs::Marker marker;
            marker.header.frame_id = frame;
            marker.header.stamp = stamp;
            marker.type = marker.CUBE;
            marker.action = marker.ADD;
            marker.ns = "white_line";
            marker.id = i;
            if(m != points[i].size()-1)
            {
                marker.pose.position.x = ((points[i])[m].x + (points[i])[m+1].x)/2;
                marker.pose.position.y = ((points[i])[m].y + (points[i])[m+1].y)/2;
                marker.pose.position.z = ((points[i])[m].z + (points[i])[m+1].z)/2;
            }
            else
            {
                marker.pose.position.x = ((points[i])[m].x + (points[i])[0].x)/2;
                marker.pose.position.y = ((points[i])[m].y + (points[i])[0].y)/2;
                marker.pose.position.z = ((points[i])[m].z + (points[i])[0].z)/2;
            }
        }
    }
    return markers;
}

void WhiteLineEstimator::sensorCallback(const sensor_msgs::ImageConstPtr& image,const sensor_msgs::PointCloud2ConstPtr& pointcloud)
{
    cv::Mat ground_mask;
    cv::Mat ground_image;
    cv::Mat src_image;
    cv_bridge::CvImagePtr cv_ptr;
    try
    {
        cv_ptr = cv_bridge::toCvCopy(image, sensor_msgs::image_encodings::BGR8);
        src_image = cv_ptr->image;
    }
    catch (cv_bridge::Exception& e)
    {
        ROS_ERROR("cv_bridge exception: %s", e.what());
        return;
    }
    boost::optional<std::vector<ProjectedPoint> > projected_points = pointcloud_projector_ptr_->project(src_image,pointcloud,ground_mask,ground_image);
    if(!projected_points)
    {
        return;
    }
    cv::Mat filterd_image;
    std::vector<std::vector<cv::Point> > contours = filter_.filterWhiteLine(src_image,ground_mask,filterd_image);
    boost::optional<std::vector<std::vector<geometry_msgs::Point> > > projected_white_line_contours;
    projected_white_line_contours = image_points_projector_ptr_->project(contours);
    if(!projected_white_line_contours)
    {
        ROS_ERROR("failed to project white line contours to the world points.");
        return;
    }
    try
    {
        cv_ptr = cv_bridge::toCvCopy(image, sensor_msgs::image_encodings::MONO8);
        cv_ptr->image = filterd_image;
        image_pub_.publish(cv_ptr->toImageMsg());
    }
    catch (cv_bridge::Exception& e)
    {
        ROS_ERROR("cv_bridge exception: %s", e.what());
        return;
    }
    return;
}

void WhiteLineEstimator::cameraInfoCallback(const sensor_msgs::CameraInfoConstPtr& msg)
{
    pointcloud_projector_ptr_->setCameraInfo(*msg);
    image_points_projector_ptr_->setCameraInfo(*msg);
    return;
}

void WhiteLineEstimator::projectionMatrixCallback(const autoware_msgs::ProjectionMatrixConstPtr& msg)
{
    pointcloud_projector_ptr_->setProjectionMatrix(*msg);
    image_points_projector_ptr_->setProjectionMatrix(*msg);
    return;
}

void WhiteLineEstimator::configureCallback(white_line_estimator::white_line_estimatorConfig &config, uint32_t level)
{
    filter_.updateParameters(config.min_white_line_area,config.max_white_line_area);
    return;
}