//
//  main.cpp
//  RobustTextDetection
//
//  Created by Saburo Okita on 05/06/14.
//  Copyright (c) 2014 Saburo Okita. All rights reserved.
//

#include <iostream>
#include <opencv2/opencv.hpp>
#include <tesseract/baseapi.h>
#include "RobustTextDetection.h"
#include "ConnectedComponent.h"

using namespace std;
using namespace cv;


int main(int argc, const char * argv[])
{
	double start = (double)getTickCount();
    Mat image = imread( "E:/project/MSER11/MSER11/img_10.jpg" );
    
    /* Quite a handful or params */
    RobustTextParam param;
    param.minMSERArea        = 50;
    param.maxMSERArea        = 50000;
    param.cannyThresh1       = 20;
    param.cannyThresh2       = 100;
    
    param.maxConnCompCount   = 6000;
    param.minConnCompArea    = 75;
    param.maxConnCompArea    = 600;
    
    param.minEccentricity    = 0.1;
    param.maxEccentricity    = 0.995;
    param.minSolidity        = 0.4;
    param.maxStdDevMeanRatio = 0.5;
    
    /* Apply Robust Text Detection */
    /* ... remove this temp output path if you don't want it to write temp image files */
    string temp_output_path = "E:/project/MSER11";
	//类的实例初始化,指定参数和中间图片输出路径
    RobustTextDetection detector(param, temp_output_path );
	//调用类的方法，对图片进行MSER连通域检测
    pair<Mat, Rect> result = detector.apply( image );
    
    /* Get the region where the candidate text is */
    Mat stroke_width( result.second.height, result.second.width, CV_8UC1, Scalar(0) );
    Mat(result.first, result.second).copyTo( stroke_width);
    
    
    /* Use Tesseract to try to decipher our image */
    tesseract::TessBaseAPI tesseract_api;
    tesseract_api.Init(NULL, "eng"  );
    tesseract_api.SetImage((uchar*) stroke_width.data, stroke_width.cols, stroke_width.rows, 1, stroke_width.cols);
    
    string out = string(tesseract_api.GetUTF8Text());

    /* Split the string by whitespace */
    vector<string> splitted;
    istringstream iss( out );
    copy( istream_iterator<string>(iss), istream_iterator<string>(), back_inserter( splitted ) );
    
    /* And draw them on screen */
	/*QtFont font = fontQt("Helvetica", 24.0, RGB(0, 0, 0));
    Point coord = Point( result.second.br().x + 10, result.second.tl().y );
    for( string& line: splitted ) {
        addText( image, line, coord, font );
        coord.y += 25;
    }*/

	for (string& line : splitted){
		cout << line << " " << endl;
	}
    
    rectangle( image, result.second, Scalar(0, 0, 255), 2);
    
    /* Append the original and stroke width images together */
    cvtColor( stroke_width, stroke_width, COLOR_GRAY2BGR );
    Mat appended( image.rows, image.cols + stroke_width.cols, CV_8UC3 );
    image.copyTo( Mat(appended, Rect(0, 0, image.cols, image.rows)) );
    stroke_width.copyTo( Mat(appended, Rect(image.cols, 0, stroke_width.cols, stroke_width.rows)) );
    
    imshow("result", appended );
	double duration = ((double)getTickCount() - start) / getTickFrequency();
	cout << "算法运行时间为： " << duration << " 秒";
    waitKey();
    
    return 0;
}
