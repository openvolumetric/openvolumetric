#pragma once


//
//
//
class IVolumetricVideo
{

public:

	//--------------------------------------------------------
	// default constructor
	IVolumetricVideo(): m_id(-1) {};


	//--------------------------------------------------------
	// constructor with instance id
	IVolumetricVideo(int id) :m_id(id) {};
	
	
	//--------------------------------------------------------
	// destructor
	~IVolumetricVideo() {};

	//--------------------------------------------------------
	// return the instance id
	int get_id() { return m_id; }
	
	//--------------------------------------------------------
	// function to implement: creation of the resources
	virtual int create_resources() = 0;


	//--------------------------------------------------------
	// function to implement: set frame
	virtual int set_frame(int frame_index) = 0;


	//--------------------------------------------------------
	// function to implement: render
	virtual int render() = 0;

	   
private:

	//--------------------------------------------------------
	// instance id 
	int m_id;



};
