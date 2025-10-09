#pragma once

#include <vector>
#include <queue>
#include <string>

#include "GL/glew.h"
#include "GLFW/glfw3.h"
#include "cuda.h"



using namespace std;

struct Timer{

	enum TimestampType{
		OPENGL = 0,
		CUDA = 1,
	};

	struct Timestamp{
		TimestampType type;
		GLuint glTimestampQuery;
		CUevent cudaEvent;
	};

	struct Recording{
		Timestamp start;
		Timestamp end;
		string label;
		double milliseconds;
	};

	inline static queue<Timestamp> pool;
	inline static bool enabled = false;
	
	inline static vector<Timestamp> timestamps;
	inline static vector<Recording> recordings;

	static void init(){
		static bool initialized = false;

		if(!initialized){

			int poolSize = 1000;

			for(int i = 0; i < poolSize; i++){

				Timestamp timestamp;
				
				glGenQueries(1, &timestamp.glTimestampQuery);
				cuEventCreate(&timestamp.cudaEvent, CU_EVENT_DEFAULT);

				pool.push(timestamp);
			}

			initialized = true;
		}
	}

	static Timestamp recordGlTimestamp(){

		if(!enabled) return Timestamp();
		init();

		Timestamp timestamp = pool.front();
		pool.pop();

		timestamp.type = TimestampType::OPENGL;
		glQueryCounter(timestamp.glTimestampQuery, GL_TIMESTAMP);

		timestamps.push_back(timestamp);

		return timestamp;
	}

	static Timestamp recordCudaTimestamp(){

		if(!enabled) return Timestamp();
		init();

		Timestamp timestamp = pool.front();
		pool.pop();

		timestamp.type = TimestampType::CUDA;
		cuEventRecord(timestamp.cudaEvent, 0);

		timestamps.push_back(timestamp);

		return timestamp;
	}

	static void recordDuration(string label, Timestamp start, Timestamp end){

		if(!enabled) return;
		init();

		Recording recording;
		recording.label = label;
		recording.start = start;
		recording.end = end;

		recordings.push_back(recording);
	}

	// Evaluate all pending timestamp queries, then clear them and put them back into the pool
	static vector<Recording> resolve(){

		if(!enabled) return vector<Recording>();
		init();

		for(Recording& recording : recordings){

			// Ignore missmatching start&end types
			if(recording.start.type != recording.end.type) continue;

			if(recording.start.type == TimestampType::OPENGL){
				// resolve OPENGL timestamp queries

				GLint done = 0;
				while (!done) {
					glGetQueryObjectiv(recording.end.glTimestampQuery, GL_QUERY_RESULT_AVAILABLE, &done);
				}

				GLuint64 startTime = 0, endTime = 0;
				glGetQueryObjectui64v(recording.start.glTimestampQuery, GL_QUERY_RESULT, &startTime);
				glGetQueryObjectui64v(recording.end.glTimestampQuery, GL_QUERY_RESULT, &endTime);

				recording.milliseconds = double(endTime - startTime) / 1'000'000.0;

			}else if(recording.start.type == TimestampType::CUDA){
				// resolve CUDA events
				cuCtxSynchronize();
				float duration;
				cuEventElapsedTime(&duration, recording.start.cudaEvent, recording.end.cudaEvent);

				recording.milliseconds = duration;
			}
		}

		for(Timestamp timestamp : timestamps){
			pool.push(timestamp);
		}

		vector<Recording> returnvalue = recordings;

		timestamps.clear();
		recordings.clear();

		return returnvalue;
	}


};