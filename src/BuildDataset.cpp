#include "TestVideoSegmentation.h"

const float parameters[] = { 2.0f,150.0f,75,0.5f,50.0f,50,50,0.1f };

using namespace std;
using namespace pcl;
using namespace cv;

void CreatePointCloudFromRegisteredNYUData(const Mat &img, const Mat &depth, PointCloudBgr *cloud) {
	//assert(!img.IsNull() && !depth.IsNull());
	//take care of old cloud to prevent memory leak/corruption
	if (cloud != NULL && cloud->size() > 0) {
		cloud->clear();
	}
	cloud->header.frame_id =  "/microsoft_rgb_optical_frame";
	cloud->height = 480;
	cloud->width = 640;
	cloud->is_dense = true;
	cloud->points.resize (cloud->height * cloud->width);
	PointCloud<PointXYZRGBA>::iterator pCloud = cloud->begin();
	Mat_<int>::const_iterator pDepth = depth.begin<int>();
	Mat_<Vec3b>::const_iterator pImg = img.begin<Vec3b>();
	for(int j = 0; j < img.rows; j++) {
		for(int i = 0; i < img.cols; i++) {
			pCloud->z = *pDepth / 1000.0f;
			pCloud->x = float((i - 3.1304475870804731e+02) * pCloud->z / 5.8262448167737955e+02);
			pCloud->y = float((j - 2.3844389626620386e+02) * pCloud->z / 5.8269103270988637e+02);
			pCloud->b = (*pImg)[0];
			pCloud->g = (*pImg)[1];
			pCloud->r = (*pImg)[2];
			pCloud->a = 255;
			pImg++; pDepth++; pCloud++;
		}
	}
	cloud->sensor_origin_.setZero ();
	cloud->sensor_orientation_.w () = 1.0;
	cloud->sensor_orientation_.x () = 0.0;
	cloud->sensor_orientation_.y () = 0.0;
	cloud->sensor_orientation_.z () = 0.0;
}

void CreateLabeledCloudFromNYUPointCloud(const PointCloudBgr &cloud, const Mat &label, PointCloudInt *labelCloud) {
	if (labelCloud != NULL && labelCloud->size() > 0) {
		labelCloud->clear();
	}
	labelCloud->header.frame_id =  cloud.header.frame_id;
	labelCloud->height = cloud.height;
	labelCloud->width = cloud.width;
	labelCloud->is_dense = true;
	labelCloud->points.resize (labelCloud->height * labelCloud->width);

	PointCloud<PointXYZRGBA>::const_iterator pCloud = cloud.begin();
	PointCloud<PointXYZI>::iterator pLabelCloud = labelCloud->begin();
	Mat_<int>::const_iterator pLabel = label.begin<int>();
	while(pCloud != cloud.end()) {
		pLabelCloud->x = pCloud->x;
		pLabelCloud->y = pCloud->y;
		pLabelCloud->z = pCloud->z;
		pLabelCloud->intensity = *pLabel;
		++pLabel; ++pCloud; ++pLabelCloud;
	}
	labelCloud->sensor_origin_.setZero ();
	labelCloud->sensor_orientation_.w () = 1.0;
	labelCloud->sensor_orientation_.x () = 0.0;
	labelCloud->sensor_orientation_.y () = 0.0;
	labelCloud->sensor_orientation_.z () = 0.0;
}

inline void EstimateNormals(const PointCloud<PointXYZRGBA>::ConstPtr &cloud, PointCloud<PointNormal>::Ptr &normals, bool fill) {
	pcl::IntegralImageNormalEstimation<pcl::PointXYZRGBA, pcl::PointNormal> ne;
	ne.setNormalEstimationMethod (ne.AVERAGE_3D_GRADIENT);
	ne.setMaxDepthChangeFactor(0.02f);
	ne.setNormalSmoothingSize(10.0f);
	ne.setInputCloud(cloud);
	ne.compute(*normals);
	if(fill) {
		PointCloudNormal::iterator p = normals->begin();
		while(p != normals->end()) {
			if(_isnan(p->normal_x))
				p->normal_x = 0;
			if(_isnan(p->normal_y))
				p->normal_y = 0;
			if(_isnan(p->normal_z))
				p->normal_z = 0;
			++p;
		}
	}
}

inline int GetClass(const PointCloudInt &cloud, const Mat &labels, int id) {
	int i, ret = 0;
	int *lookup = new int[NUM_LABELS];
	for(i = 0; i < NUM_LABELS; i++)
		lookup[i] = 0;
	PointCloudInt::const_iterator p = cloud.begin();
	Mat_<int>::const_iterator pL = labels.begin<int>();
	while(p != cloud.end()) {
		if(p->intensity == id)
			lookup[*pL]++;
		++p; ++pL;
	}
	int max = lookup[0], maxLoc = 0;
	for(i = 0; i < NUM_LABELS; i++) {
		if(lookup[i] > max) {
			max = lookup[i];
			maxLoc = i;
		}
	}
	try {
		delete[] lookup;
	} catch(...) {
		cout << "small error here" << endl;
	}
	return maxLoc;
}

inline float HistTotal(LABXYZUVW *hist) {
	float tot = 0.0f;
	for(int k = 0; k < NUM_BINS; k++) {
		tot += hist[k].u;
	}
	return tot;
}

inline void CalcMask(const PointCloudInt &cloud, int id, Mat &mask) {
	PointCloudInt::const_iterator pC = cloud.begin();
	uchar *pM = mask.data;
	while(pC != cloud.end()) {
		if(pC->intensity == id)
			*pM = 255;
		++pM; ++pC;
	}
}

void GetFeatureVectors(FILE *fp, Classifier &c, const RegionTree3D &tree, Mat &img, const PointCloudInt &cloud, const Mat &label, const int numImage) {
	//for each top level region, I need to give it a class name.
	int k;
	vector<Region3D*>::const_iterator p = tree.top_regions.begin();
	for(int i = 0; i < tree.top_regions.size(); i++, p++) {
		//Calculate mask
		Mat desc, mask = Mat::zeros(img.size(),CV_8UC1);
		CalcMask(cloud,(*p)->m_centroid3D.intensity,mask);
		//get features
		c.CalculateBOWFeatures(img,mask,desc);
		if(desc.empty())
			desc = Mat::zeros(1,500,CV_32F);
		int id = GetClass(cloud,label,(*p)->m_centroid3D.intensity);
		fprintf(fp,"%d,%f,%f,%f,%f,%f",(*p)->m_size,(*p)->m_centroid.x,(*p)->m_centroid.y,(*p)->m_centroid3D.x,(*p)->m_centroid3D.y,(*p)->m_centroid3D.z);
		float a = ((*p)->m_max3D.x - (*p)->m_min3D.x), b = ((*p)->m_max3D.y - (*p)->m_min3D.y), c = ((*p)->m_max3D.z - (*p)->m_min3D.z);
		fprintf(fp,",%f,%f,%f,%f,%f,%f,%f,%f",(*p)->m_min3D.x,(*p)->m_min3D.y,(*p)->m_min3D.z,(*p)->m_max3D.x,(*p)->m_max3D.y,(*p)->m_max3D.z,sqrt(a*a + c*c),b);
		//LABXYZUVW *p1 = (*p)->m_hist;
		//float tot = HistTotal((*p)->m_hist);
		for(k = 0; k < NUM_BINS; k++)
			fprintf(fp,",%f",float((*p)->m_hist[k].a)/(*p)->m_size);
		for(k = 0; k < NUM_BINS; k++)
			fprintf(fp,",%f",float((*p)->m_hist[k].b)/(*p)->m_size);
		for(k = 0; k < NUM_BINS; k++)
			fprintf(fp,",%f",float((*p)->m_hist[k].l)/(*p)->m_size);
		for(k = 0; k < NUM_BINS; k++)
			fprintf(fp,",%f",(*p)->m_hist[k].u/(*p)->m_size);
		for(k = 0; k < NUM_BINS; k++)
			fprintf(fp,",%f",(*p)->m_hist[k].v/(*p)->m_size);
		for(k = 0; k < NUM_BINS; k++)
			fprintf(fp,",%f",(*p)->m_hist[k].w/(*p)->m_size);
		for(k = 0; k < NUM_BINS_XYZ; k++)
			fprintf(fp,",%f",(*p)->m_hist[k].x/(*p)->m_size);
		for(k = 0; k < NUM_BINS_XYZ; k++)
			fprintf(fp,",%f",(*p)->m_hist[k].y/(*p)->m_size);
		for(k = 0; k < NUM_BINS_XYZ; k++)
			fprintf(fp,",%f",(*p)->m_hist[k].z/(*p)->m_size);
		float *pD = (float*)desc.data;
		for(k = 0; k < desc.cols; k++, pD++)
			fprintf(fp,",%f",*pD);
		fprintf(fp,",%d,%d\n",numImage,id);
	}
}

void GetMatFromRegion(Region3D *reg, vector<float> &sample, int sample_size) {
	int k;
	sample.resize(sample_size);
	vector<float>::iterator p = sample.begin();
	*p++ = float(reg->m_size);
	*p++ = reg->m_centroid.x;
	*p++ = reg->m_centroid.y;
	*p++ = reg->m_centroid3D.x;
	*p++ = reg->m_centroid3D.y;
	*p++ = reg->m_centroid3D.z;
	float a = (reg->m_max3D.x - reg->m_min3D.x), b = (reg->m_max3D.y - reg->m_min3D.y), c = (reg->m_max3D.z - reg->m_min3D.z);
	*p++ = reg->m_min3D.x;
	*p++ = reg->m_min3D.y;
	*p++ = reg->m_min3D.z;
	*p++ = reg->m_max3D.x;
	*p++ = reg->m_max3D.y;
	*p++ = reg->m_max3D.z;
	*p++ = sqrt(a*a+c*c);
	*p++ = b;
	for(k = 0; k < NUM_BINS; k++)
		*p++ = reg->m_hist[k].a / reg->m_size;
	for(k = 0; k < NUM_BINS; k++)
		*p++ = reg->m_hist[k].b / reg->m_size;
	for(k = 0; k < NUM_BINS; k++)
		*p++ = reg->m_hist[k].l / reg->m_size;
	for(k = 0; k < NUM_BINS; k++)
		*p++ = reg->m_hist[k].u / reg->m_size;
	for(k = 0; k < NUM_BINS; k++)
		*p++ = reg->m_hist[k].v / reg->m_size;
	for(k = 0; k < NUM_BINS; k++)
		*p++ = reg->m_hist[k].w / reg->m_size;
	for(k = 0; k < NUM_BINS_XYZ; k++)
		*p++ = reg->m_hist[k].x / reg->m_size;
	for(k = 0; k < NUM_BINS_XYZ; k++)
		*p++ = reg->m_hist[k].y / reg->m_size;
	for(k = 0; k < NUM_BINS_XYZ; k++, p++)
		*p = reg->m_hist[k].z / reg->m_size;
}

void BuildNYUDataset(string direc) {
	srand(time(NULL));
	PointCloudBgr cloud,segment;
	PointCloudInt labelCloud;
	Mat img, depth, label;
	boost::shared_ptr<pcl::PointCloud<pcl::PointNormal> > normals(new pcl::PointCloud<pcl::PointNormal>);
	//open training file
	Classifier c(direc);
	c.LoadTrainingInd();
	c.load_vocab();
	FILE *fp = fopen("features.txt","wb");
	if(fp == NULL)
		throw exception("Couldn't open features file");
	/*fprintf(fp,"size,cx,cy,c3x,c3y,c3z,minx,miny,minz,maxx,maxy,maxz,xdist,ydist");
	for(int j = 0; j < 9; j++) {
	for(int k = 0; k < (j < 6 ? NUM_BINS : NUM_BINS_XYZ); k++) {
	fprintf(fp,",h%d_%d",j,k);
	}
	}
	fprintf(fp,",frame,class\n");*/
	for(int i = 1; i < 1450; i++) {
		if(i == c.trainingInds.front()) {
			c.trainingInds.pop_front();
			cout << i << endl;
			LoadData(direc,i,img,depth,label);
			CreatePointCloudFromRegisteredNYUData(img,depth,&cloud);
			//CreateLabeledCloudFromNYUPointCloud(cloud,label,&labelCloud);
			int segments = SHGraphSegment(cloud,parameters[0],parameters[1],parameters[2],parameters[3],parameters[4],parameters[5],&labelCloud,&segment);
			EstimateNormals(cloud.makeShared(),normals,false);
			RegionTree3D tree;
			tree.Create(cloud,labelCloud,*normals,segments,0);
			tree.PropagateRegionHierarchy(parameters[6]);
			tree.ImplementSegmentation(parameters[7]);
			
			GetFeatureVectors(fp,c,tree,img,labelCloud,label,i);
			
			//release stuff
			segment.clear();
			cloud.clear();
			labelCloud.clear();
			img.release();
			depth.release();
			label.release();
			normals->clear();
			tree.top_regions.clear();
			tree.Release();
		}
	}
	fclose(fp);
}

void BuildRFClassifier(string direc) {
	FILE *fp = fopen("features.txt","rb");
	if(fp == NULL)
		throw exception("Couldn't open features file");
	int i,j,length,tmp,num = 14 + 6*NUM_BINS + 3*NUM_BINS_XYZ;
	vector<vector<float>> features;
	vector<int> labels;
	features.reserve(200000);
	labels.reserve(200000);
	while(!feof(fp)) {
		vector<float> feature_vec;
		feature_vec.resize(num);
		fscanf(fp,"%d",&tmp);
		feature_vec[0] = float(tmp);
		for(i = 1; i < num; i++) {
			float tmp_f;
			fscanf(fp,",%f",&tmp_f);
			feature_vec[i] = tmp_f;
		}
		fscanf(fp,",%d",&tmp);
		fscanf(fp,",%d\n",&tmp);
		labels.push_back(tmp);
		features.push_back(feature_vec);
	}
	fclose(fp);
	Classifier c(direc);
	c.LoadClass4Map();
	vector<int>::iterator pL = labels.begin();
	while(pL != labels.end()) {
		*pL = c.classMap[*pL];
		++pL;
	}
	//we should convert this to Mats
	Mat labelMat = Mat(labels);
	labels.clear();
	Mat featureMat = Mat(features.size(),num,CV_32F);
	float *p = (float *)featureMat.data;
	vector<vector<float>>::iterator pI = features.begin();
	for(i=0;i<features.size();i++) {
		vector<float> pTmp = *pI;
		for(j=0;j<num;j++) {
			*p = pTmp[j];
			//featureMat.at<float>(i,j) = pTmp[j];
			//assert(*p == featureMat.at<float>(i,j));
			++p;
		}
		pTmp.clear();
		++pI;
	}
	features.clear();

	// define all the attributes as numerical
	// alternatives are CV_VAR_CATEGORICAL or CV_VAR_ORDERED(=CV_VAR_NUMERICAL)
	// that can be assigned on a per attribute basis
	Mat var_type = Mat(num + 1, 1, CV_8U );
	var_type.setTo(Scalar(CV_VAR_NUMERICAL) ); // all inputs are numerical
	// this is a classification problem (i.e. predict a discrete number of class
	// outputs) so reset the last (+1) output var_type element to CV_VAR_CATEGORICAL
	var_type.at<uchar>(num, 0) = CV_VAR_CATEGORICAL;
	//float priors[] = {1,1};
	CvRTParams params = CvRTParams(25, // max depth
		5, // min sample count
		0, // regression accuracy: N/A here
		false, // compute surrogate split, no missing data
		15, // max number of categories (use sub-optimal algorithm for larger numbers)
		nullptr, // the array of priors
		false,  // calculate variable importance
		100,       // number of variables randomly selected at node and used to find the best split(s).
		100,	 // max number of trees in the forest
		0.01f,				// forrest accuracy
		CV_TERMCRIT_ITER |	CV_TERMCRIT_EPS // termination cirteria
		);

	// train random forest classifier (using training data)
	CvRTrees* rtree = new CvRTrees;

	rtree->train(featureMat, CV_ROW_SAMPLE, labelMat,
		Mat(), Mat(), var_type, Mat(), params);
	rtree->save("rf.xml");
	delete rtree;
}

void TestRFClassifier(string direc) {
	PointCloudBgr cloud,segment;
	PointCloudInt labelCloud;
	Mat img, depth, label;
	boost::shared_ptr<pcl::PointCloud<pcl::PointNormal> > normals(new pcl::PointCloud<pcl::PointNormal>);
	//open training file
	Classifier c(direc);
	c.LoadTestingInd();
	c.LoadClass4Map();
	CvRTrees* rtree = new CvRTrees;
	rtree->load("rf.xml");
	Mat conf = Mat::zeros(5,5,CV_32S);
	Mat confClass = Mat::zeros(5,5,CV_32S);
	for(int i = 1; i < 1450; i++) {
		if(i == c.testingInds.front()) {
			c.testingInds.pop_front();
			cout << i << endl;
			LoadData(direc,i,img,depth,label);
			CreatePointCloudFromRegisteredNYUData(img,depth,&cloud);
			//CreateLabeledCloudFromNYUPointCloud(cloud,label,&labelCloud);
			int segments = SHGraphSegment(cloud,parameters[0],parameters[1],parameters[2],parameters[3],parameters[4],parameters[5],&labelCloud,&segment);
			EstimateNormals(cloud.makeShared(),normals,false);
			RegionTree3D tree;
			tree.Create(cloud,labelCloud,*normals,segments,0);
			tree.PropagateRegionHierarchy(parameters[6]);
			tree.ImplementSegmentation(parameters[7]);
			/*viewer.removePointCloud("cloud");
			viewer.removePointCloud("original");
			viewer.addPointCloud(segment.makeShared(),"original");
			viewer.addPointCloudNormals<pcl::PointXYZRGBA,pcl::PointNormal>(segment.makeShared(), normals);
			while(1)
			viewer.spinOnce();*/
			int result, feature_len = 14 + 6*NUM_BINS + 3*NUM_BINS_XYZ;
			vector<Region3D*>::const_iterator p = tree.top_regions.begin();
			for(int i = 0; i < tree.top_regions.size(); i++, p++) {
				vector<float> sample;
				GetMatFromRegion(*p,sample,feature_len);
				Mat sampleMat = Mat(sample);
				result = Round(rtree->predict(sampleMat));
				assert(result > 0 && result <= 4);
				confClass.at<int>(result,c.classMap[GetClass(labelCloud,label,(*p)->m_centroid3D.intensity)])++;
				tree.SetBranch(*p,0,result);
			}

			Mat myResult, groundTruth, myResultColor, groundTruthColor, labelColor;
			myResult = Mat(label.rows,label.cols,label.type());
			groundTruth = Mat(label.rows,label.cols,label.type());
			PointCloudInt::iterator pC = labelCloud.begin();
			int *pNewL = (int*)groundTruth.data;
			int *pNewC = (int*)myResult.data;
			int *pL = (int *)label.data;
			while(pC != labelCloud.end()) {
				int newLabel = c.classMap[*pL];
				*pNewL = newLabel;
				*pNewC = pC->intensity;
				/*if(newLabel < 0 || newLabel > 4)
				cout << "label is: " << newLabel << endl;
				if(pC->intensity < 0 || pC->intensity > 4)
				cout << "result is: " << pC->intensity << endl;
				else*/
				conf.at<int>(pC->intensity,newLabel)++;
				++pL; ++pC; ++pNewL; ++pNewC;
			}
			/*groundTruth.convertTo(groundTruth,CV_8UC1,63,0);
			myResult.convertTo(myResult,CV_8UC1,63,0);
			label.convertTo(labelColor,CV_8UC1,894,0);
			applyColorMap(groundTruth,groundTruthColor,COLORMAP_JET);
			applyColorMap(myResult,myResultColor,COLORMAP_JET);
			imshow("color",img);
			imshow("original label",labelColor);
			imshow("label",groundTruthColor);
			imshow("result",myResultColor);
			waitKey();*/

			//release stuff
			myResult.release();
			groundTruth.release();
			myResultColor.release();
			groundTruthColor.release();
			segment.clear();
			cloud.clear();
			labelCloud.clear();
			img.release();
			depth.release();
			label.release();
			normals->clear();
			tree.top_regions.clear();
			tree.Release();
		}
	}

	float tot = 0, result = 0;
	int x,y;
	for(x=1; x<5; x++) {
		for(y=1; y<5; y++) {
			cout << conf.at<int>(x,y) << ", ";
			tot += conf.at<int>(x,y);
			if(x == y)
				result += conf.at<int>(x,y);
		}
		cout << endl;
	}
	cout << "Accuracy: " << (result / tot) << endl;
	cout << endl;
	tot = 0; result = 0;
	for(x=1; x<5; x++) {
		for(y=1; y<5; y++) {
			cout << confClass.at<int>(x,y) << ", ";
			tot += confClass.at<int>(x,y);
			if(x == y)
				result += confClass.at<int>(x,y);
		}
		cout << endl;
	}
	cout << "Class Accuracy: " << (result / tot) << endl;

	delete rtree;
}