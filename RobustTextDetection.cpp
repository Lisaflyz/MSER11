//
//  RobustTextDetection.cpp
//  RobustTextDetection
//
//  Created by Saburo Okita on 08/06/14.
//  Copyright (c) 2014 Saburo Okita. All rights reserved.
//

#include "RobustTextDetection.h"
#include "ConnectedComponent.h"
#include<opencv2/opencv.hpp>
#include <numeric>

using namespace std;
using namespace cv;

RobustTextDetection::RobustTextDetection(string temp_img_directory) {
}

RobustTextDetection::RobustTextDetection(RobustTextParam & param, string temp_img_directory) {
    this->param                 = param;
    this->tempImageDirectory    = temp_img_directory;
}

/**
 * Apply robust text detection algorithm
 * It returns the filtered stroke width image which contains the possible
 * text in binary format, and also the rect
 *进行字符检测算法，返回根据笔划宽度滤除后的区域，字符形式为二值化，且有矩形
 **/
pair<Mat, Rect> RobustTextDetection::apply( Mat& image ) {
    Mat grey      = preprocessImage( image );//图片灰度化
    Mat mser_mask = createMSERMask( grey );//返回的是mask,前景为白色，背景为黑色
    
    
    /* Perform canny edge operator to extract the edges */
	//canny边缘检测得到边缘图像
    Mat edges;
    Canny( grey, edges, param.cannyThresh1, param.cannyThresh2 );
    
    
    /* Create the edge enhanced MSER region */
	//创建边缘增强的MSER区域
	//原文中是指在MSER过程中得知文字是暗还是亮，将边缘梯度调整为指向背景方向，从而消除内部和粘连的MSER区域

	//代码中的实现不同
    Mat edge_mser_intersection  = edges & mser_mask;//既是边缘，又是MSER区域
    Mat gradient_grown          = growEdges( grey, edge_mser_intersection );
    Mat edge_enhanced_mser      = ~gradient_grown & mser_mask;//切割出边缘线
    
    /* Writing temporary output images */
    if( !tempImageDirectory.empty() ) {
        cout << "Writing temp output images" << endl;
        imwrite( tempImageDirectory + "/out_grey.png",                   grey );
        imwrite( tempImageDirectory + "/out_mser_mask.png",              mser_mask );
        imwrite( tempImageDirectory + "/out_canny_edges.png",            edges );
        imwrite( tempImageDirectory + "/out_edge_mser_intersection.png", edge_mser_intersection );
        imwrite( tempImageDirectory + "/out_gradient_grown.png",         gradient_grown );
        imwrite( tempImageDirectory + "/out_edge_enhanced_mser.png",     edge_enhanced_mser );
    }
    
    /* Find the connected components */
	//4邻域
    ConnectedComponent conn_comp( param.maxConnCompCount, 4);
    Mat labels = conn_comp.apply( edge_enhanced_mser );
    vector<ComponentProperty> props = conn_comp.getComponentsProperties();
    
    
    Mat result( labels.size(), CV_8UC1, Scalar(0));//result初始化为全0
	//过滤规则的参数
    for( ComponentProperty& prop: props ) {
        /* Filtered out connected components that aren't within the criteria */
        if( prop.area < param.minConnCompArea || prop.area > param.maxConnCompArea )
            continue;
        
        if( prop.eccentricity < param.minEccentricity || prop.eccentricity > param.maxEccentricity )
            continue;
        
        if( prop.solidity < param.minSolidity )
            continue;
        
        result |= (labels == prop.labelID);//靠保留下来的连通域显示在最后的结果中
    }
    

    /* Calculate the distance transformed from the connected components */
    cv::distanceTransform( result, result, DIST_L2, 3 );
    result.convertTo( result, CV_32SC1 );
    
    /* Find the stroke width image from the distance transformed */
    Mat stroke_width = computeStrokeWidth( result );
    
    /* Filter the stroke width using connected component again */
    conn_comp   = ConnectedComponent( param.maxConnCompCount, 4);
    labels      = conn_comp.apply( stroke_width );
    props       = conn_comp.getComponentsProperties();
    
    Mat filtered_stroke_width( stroke_width.size(), CV_8UC1, Scalar(0) );
    
    for( ComponentProperty& prop: props ) {
        Mat mask = labels == prop.labelID;
        Mat temp;
        stroke_width.copyTo( temp, mask );
        
        int area = countNonZero( temp );
        
        /* Since we only want to consider the connected component, ignore the zero pixels */
        vector<int> vec = Mat( temp.reshape( 1, temp.rows * temp.cols ) );
        vector<int> nonzero_vec;
        copy_if( vec.begin(), vec.end(), back_inserter(nonzero_vec), [&](int val){
            return val > 0;
        });
        
        /* Find mean and std deviation for the connected components */
        double mean = std::accumulate( nonzero_vec.begin(), nonzero_vec.end(), 0.0 ) / area;
        
        double accum = 0.0;
        for( int val: nonzero_vec )
            accum += (val - mean) * (val - mean );
        double std_dev = sqrt( accum / area );
        
        /* Filter out those which are out of the prespecified ratio */
        if( (std_dev / mean) > param.maxStdDevMeanRatio  )
            continue;
        
        /* Collect the filtered stroke width */
        filtered_stroke_width |= mask;
    }

    /* Use morphological close and open to create a large connected bounding region from the filtered stroke width */
    Mat bounding_region;
    morphologyEx( filtered_stroke_width, bounding_region, MORPH_CLOSE, getStructuringElement( MORPH_ELLIPSE, Size(25, 25)) );
    morphologyEx( bounding_region, bounding_region, MORPH_OPEN, getStructuringElement( MORPH_ELLIPSE, Size(7, 7)) );
    
    /* ... so that we can get an overall bounding rect */
    Mat bounding_region_coord;
    findNonZero( bounding_region, bounding_region_coord );
	//返回一个Rect，包含所有点集
    Rect bounding_rect = boundingRect( bounding_region_coord );
	//cout << bounding_rect.tl.x << " " << bounding_rect.tl.x << bounding_rect.br.x << bounding_rect.br.y << endl;
    Mat bounding_mask( filtered_stroke_width.size(), CV_8UC1, Scalar(0) );
    Mat( bounding_mask, bounding_rect ) = 255;
    
    /* Well, add some margin to the bounding rect */
    bounding_rect = Rect( bounding_rect.tl() - Point(5, 5), bounding_rect.br() + Point(5, 5) );
    bounding_rect = clamp( bounding_rect, image.size() );
    
    
    /* Well, discard everything outside of the bounding rectangle */
    filtered_stroke_width.copyTo( filtered_stroke_width, bounding_mask );
    
    return pair<Mat, Rect>( filtered_stroke_width, bounding_rect );
}


//将矩形区域适应到原图范围中
Rect RobustTextDetection::clamp( Rect& rect, Size size ) {
    Rect result = rect;
    
    if( result.x < 0 )
        result.x = 0;
    
    if( result.x + result.width > size.width )
        result.width = size.width - result.x;
    
    if( result.y < 0 )
        result.y = 0;
    
    if( result.y + result.height > size.height )
        result.height = size.height - result.y;
    
    return result;
}


/**
 * Create a mask out from the MSER components
 对灰度图像进行处理，为MSER区域创建掩膜
 */
Mat RobustTextDetection::createMSERMask( Mat& grey ) {
    /* Find MSER components */
    vector<vector<Point>> contours;//存储MSER的点
	/*MSER各个参数的意义
	MSER(int _delta, int _min_area, int _max_area,
		float _max_variation, float _min_diversity,
		int _max_evolution, double _area_threshold,
		double _min_margin, int _edge_blur_size);*/
    //MSER mser( 8, param.minMSERArea, param.maxMSERArea, 0.25, 0.1, 100, 1.01, 0.03, 5 );
	//最大变动，最小差异
	MSER mser(8, param.minMSERArea, param.maxMSERArea, 0.25, 0.1);
    mser(grey, contours);
    
    /* Create a binary mask out of the MSER */
	//创建二值化的图显示，为MSER的区域为白色，背景为黑色
    Mat mser_mask( grey.size(), CV_8UC1, Scalar(0));
    
    for( int i = 0; i < contours.size(); i++ ) {
        for( Point& point: contours[i] )
            mser_mask.at<uchar>(point) = 255;
    }
	imshow("msermask", mser_mask);
    return mser_mask;
	
}


/**
 * Preprocess image
 预处理图片
 */
Mat RobustTextDetection::preprocessImage( Mat& image ) {
    /* TODO: Should do contrast enhancement here  */
	//增强图片对比度对黑字的效果不明显，会消除黑字
	//Mat enlargedContrast;
	//double alpha = 1.5;
	//double beta = 10;
	//enlargedContrast = Mat::zeros(image.size(), image.type());
	//for (int i = 0; i < image.rows; ++i)
	//	for (int j = 0; j < image.cols; ++j)
	//		for (int k = 0; k < 3; ++k)
	//			//saturate有溢出功能保护的类型转换
	//			enlargedContrast.at<Vec3b>(i, j)[k] = saturate_cast<uchar>(image.at<Vec3b>(i, j)[k] * alpha + beta);

	//imshow("对比度增强",enlargedContrast);


    Mat grey;
    cvtColor( image, grey, COLOR_BGR2GRAY );
    return grey;
}

/**
 * From the angle convert into our neighborhood encoding
 * which has the following scheme
 * | 2 | 3 | 4 |
 * | 1 | 0 | 5 |
 * | 8 | 7 | 6 |
 */
//neighbors有默认值8
int RobustTextDetection::toBin( const float angle, const int neighbors ) {
    const float divisor = 180.0 / neighbors;//22.5
    return static_cast<int>( (( floor(angle / divisor)  - 1) / 2) + 1 ) % neighbors + 1;
}

/**
 * Grow the edges along with directon of gradient
 沿梯度方向对边进行增长
 使边缘线变粗
 */
Mat RobustTextDetection::growEdges(Mat& image, Mat& edges ) {
    CV_Assert( edges.type() == CV_8UC1 );
    
    Mat grad_x, grad_y;
    Sobel( image, grad_x, CV_32FC1, 1, 0 );
    Sobel( image, grad_y, CV_32FC1, 0, 1 );
    
    Mat grad_mag, grad_dir;
	//计算梯度的大小和角度，以角度计算
	//算出每一点梯度的方向
    cartToPolar( grad_x, grad_y, grad_mag, grad_dir, true );
    
    
    /* Convert the angle into predefined 3x3 neighbor locations
     | 2 | 3 | 4 |
     | 1 | 0 | 5 |
     | 8 | 7 | 6 |
     */
    for( int y = 0; y < grad_dir.rows; y++ ) {
        float * grad_ptr = grad_dir.ptr<float>(y);
        
        for( int x = 0; x < grad_dir.cols; x++ ) {
            if( grad_ptr[x] != 0 )
                grad_ptr[x] = toBin( grad_ptr[x] );
        }
    }
    grad_dir.convertTo( grad_dir, CV_8UC1 );
    
    
    
    /* Perform region growing based on the gradient direction */
	//基于梯度方向的区域生长
    Mat result = edges.clone();
    
    uchar * prev_ptr = result.ptr<uchar>(0);
    uchar * curr_ptr = result.ptr<uchar>(1);
    
    for( int y = 1; y < edges.rows - 1; y++ ) {
        uchar * edge_ptr = edges.ptr<uchar>(y);
        uchar * grad_ptr = grad_dir.ptr<uchar>(y);
        uchar * next_ptr = result.ptr<uchar>(y + 1);
        
        for( int x = 1; x < edges.cols - 1; x++ ) {
            /* Only consider the contours */
            if( edge_ptr[x] != 0 ) {
                
                /* .. there should be a better way .... */
                switch( grad_ptr[x] ) {
                    case 1: curr_ptr[x-1] = 255; break;
                    case 2: prev_ptr[x-1] = 255; break;
                    case 3: prev_ptr[x  ] = 255; break;
                    case 4: prev_ptr[x+1] = 255; break;
                    case 5: curr_ptr[x+1  ] = 255; break;
                    case 6: next_ptr[x+1] = 255; break;
                    case 7: next_ptr[x  ] = 255; break;
                    case 8: next_ptr[x-1] = 255; break;
                    default: break;
                }
            }
        }
        
        prev_ptr = curr_ptr;
        curr_ptr = next_ptr;
    }
    
    return result;
}


/**
 * Convert from our encoded 8 bit uchar to the (8) neighboring coordinates
 */
vector<Point> RobustTextDetection::convertToCoords( int x, int y, bitset<8> neighbors ) {
    vector<Point> coords;
    
    if( neighbors[0] ) coords.push_back( Point(x - 1, y    ) );
    if( neighbors[1] ) coords.push_back( Point(x - 1, y - 1) );
    if( neighbors[2] ) coords.push_back( Point(x    , y - 1) );
    if( neighbors[3] ) coords.push_back( Point(x + 1, y - 1) );
    if( neighbors[4] ) coords.push_back( Point(x + 1, y    ) );
    if( neighbors[5] ) coords.push_back( Point(x + 1, y + 1) );
    if( neighbors[6] ) coords.push_back( Point(x    , y + 1) );
    if( neighbors[7] ) coords.push_back( Point(x - 1, y + 1) );
    
    return coords;
}

/**
 * Overloaded function for convertToCoords
 */
vector<Point> RobustTextDetection::convertToCoords( Point& coord, bitset<8> neighbors ) {
    return convertToCoords( coord.x, coord.y, neighbors );
}

/**
 * Overloaded function for convertToCoords
 */
vector<Point> RobustTextDetection::convertToCoords( Point& coord, uchar neighbors ) {
    return convertToCoords( coord.x, coord.y, bitset<8>(neighbors) );
}

/**
 * Get a set of 8 neighbors that are less than given value
 * | 2 | 3 | 4 |
 * | 1 | 0 | 5 |
 * | 8 | 7 | 6 |
 */
inline bitset<8> RobustTextDetection::getNeighborsLessThan( int * curr_ptr, int x, int * prev_ptr, int * next_ptr ) {
    bitset<8> neighbors;
    neighbors[0] = curr_ptr[x-1] == 0 ? 0 : curr_ptr[x-1] < curr_ptr[x];
    neighbors[1] = prev_ptr[x-1] == 0 ? 0 : prev_ptr[x-1] < curr_ptr[x];
    neighbors[2] = prev_ptr[x  ] == 0 ? 0 : prev_ptr[x]   < curr_ptr[x];
    neighbors[3] = prev_ptr[x+1] == 0 ? 0 : prev_ptr[x+1] < curr_ptr[x];
    neighbors[4] = curr_ptr[x+1] == 0 ? 0 : curr_ptr[x+1] < curr_ptr[x];
    neighbors[5] = next_ptr[x+1] == 0 ? 0 : next_ptr[x+1] < curr_ptr[x];
    neighbors[6] = next_ptr[x  ] == 0 ? 0 : next_ptr[x]   < curr_ptr[x];
    neighbors[7] = next_ptr[x-1] == 0 ? 0 : next_ptr[x-1] < curr_ptr[x];
    return neighbors;
}



/**
 * Compute the stroke width image out from the distance transform matrix
 * It will propagate the max values of each connected component from the ridge
 * to outer boundaries
 **/
Mat RobustTextDetection::computeStrokeWidth( Mat& dist ) {
    /* Pad the distance transformed matrix to avoid boundary checking */
    Mat padded( dist.rows + 1, dist.cols + 1, dist.type(), Scalar(0) );
    dist.copyTo( Mat( padded, Rect(1, 1, dist.cols, dist.rows ) ) );
    
    Mat lookup( padded.size(), CV_8UC1, Scalar(0) );
    int * prev_ptr = padded.ptr<int>(0);
    int * curr_ptr = padded.ptr<int>(1);
    
    for( int y = 1; y < padded.rows - 1; y++ ) {
        uchar * lookup_ptr  = lookup.ptr<uchar>(y);
        int * next_ptr      = padded.ptr<int>(y+1);
        
        for( int x = 1; x < padded.cols - 1; x++ ) {
            /* Extract all the neighbors whose value < curr_ptr[x], encoded in 8-bit uchar */
            if( curr_ptr[x] != 0 )
                lookup_ptr[x] = static_cast<uchar>( getNeighborsLessThan(curr_ptr, x, prev_ptr, next_ptr).to_ullong() );
        }
        prev_ptr = curr_ptr;
        curr_ptr = next_ptr;
    }
    
    
    /* Get max stroke from the distance transformed */
    double max_val_double;
    minMaxLoc( padded, 0, &max_val_double );
    int max_stroke = static_cast<int>(round( max_val_double ));
    
    
    for( int stroke = max_stroke; stroke > 0; stroke-- ) {
        Mat stroke_indices_mat;
        findNonZero( padded == stroke, stroke_indices_mat );
        
        vector<Point> stroke_indices;
        stroke_indices_mat.copyTo( stroke_indices );
        
        vector<Point> neighbors;
        for( Point& stroke_index : stroke_indices ) {
            vector<Point> temp = convertToCoords( stroke_index, lookup.at<uchar>(stroke_index) );
            neighbors.insert( neighbors.end(), temp.begin(), temp.end() );
        }
        
        while( !neighbors.empty() ){
            for( Point& neighbor: neighbors )
                padded.at<int>(neighbor) = stroke;
            
            neighbors.clear();
            
            vector<Point> temp( neighbors );
            neighbors.clear();
            
            /* Recursively gets neighbors of the current neighbors */
            for( Point& neighbor: temp ) {
                vector<Point> temp = convertToCoords( neighbor, lookup.at<uchar>(neighbor) );
                neighbors.insert( neighbors.end(), temp.begin(), temp.end() );
            }
        }
    }
    
    return Mat( padded, Rect(1, 1, dist.cols, dist.rows) );
}


