#include "interface/functions.hpp"

#include <utility>
#include <queue>
#include <thread>
#include <chrono>
#include <cmath>

#include "common/defs.hpp"
#include "common/matrix.hpp"
#include "common/state.hpp"
#include "interface/config.hpp"
#include "image/image.hpp"
#include "model/evidence.hpp"
#include "model/system.hpp"

FILE* openStream(const std::string name, const char* mode)
{
	FILE* f = NULL;
	while (true)
	{
		f = fopen(name.c_str(), mode);
		if (f) return f;
		else std::this_thread::sleep_for(std::chrono::milliseconds(10));
	}
}

bool hydrophones(Data* data, const std::string in_name, const std::string out_name)
{
	FILE* in = openStream(in_name, "r");
	FILE* out = openStream(out_name, "w");

	bool quit = false;
	while (!quit)
	{
		float theta, phi;
		fscanf(in, " h %f %f", &theta, &phi);

		float x_i = data->state.x();
		float y_i = data->state.y();
		float depth_i = data->state.depth();
		float yaw_i = data->state.yaw();

		float distance = (15-depth_i) * std::tan((-phi) * 2*M_PI);
		float small_angle = .05;

		float x = x_i + distance * std::cos((theta + yaw_i) * 2*M_PI);
		float y = y_i + distance * std::sin((theta + yaw_i) * 2*M_PI);
		float s = (15-depth_i) * (std::tan((-phi+small_angle) * 2*M_PI) - std::tan((-phi-small_angle) * 2*M_PI));

		data->lock();
			data->evidence.push(Evidence({
				{M_PINGER_X, x, s},
				{M_PINGER_Y, y, s},
			}));
		data->unlock();
	}

	return true;
}

bool vision(Data* data, const std::string in_name, const std::string out_name, size_t vpid, size_t image_index)
{
	FILE* in = openStream(in_name, "r");
	FILE* out = openStream(out_name, "w");

	uint32_t imageID = {};

	bool quit = false;
	while (!quit)
	{
		// check if we should process this frame
		bool running;
		data->lock();
			running = data->run_vision_process.at(vpid);
		data->unlock();

		if (running)
		{
			bool newImage = false;
			cv::Mat img;
			data->lock();
				if (data->imageID.at(image_index) > imageID)
				{
					img = data->image.at(image_index);
					imageID = data->imageID.at(image_index);
					newImage = true;
				}
			data->unlock();

			if (newImage)
			{
				imageWrite(out, img);

				Evidence evidence(in);

				data->lock();
					data->evidence.push(evidence);
				data->unlock();
			}
			else std::this_thread::sleep_for(std::chrono::milliseconds(10));
		}
		else std::this_thread::sleep_for(std::chrono::milliseconds(100));
	}

	return true;
}

bool camera(Data* data, const std::string in_name, const std::string out_name, size_t image_index)
{
	FILE* in = openStream(in_name, "r");
	FILE* out = openStream(out_name, "w");

	bool quit = false;
	while (!quit)
	{
		fprintf(out, "i\n"); // request image
		fflush(out);
		auto img = imageRead(in); // read image

		// store image
		data->lock();
			data->image.at(image_index) = std::move(img);
			data->imageID.at(image_index)++;
		data->unlock();

		std::this_thread::sleep_for(std::chrono::milliseconds(100));
	}

	return true;
}

bool mission(Data* data, const std::string in_name, const std::string out_name)
{
	FILE* in = openStream(in_name, "r");
	FILE* out = openStream(out_name, "w");

	bool quit = false;
	while (!quit)
	{
		char m;
		fscanf(in, " %c", &m);
		switch (m)
		{
			case 'q': // query
			{
				char q;
				fscanf(in, " %c", &q);
				switch (q)
				{
					case 's': // state
					{
						data->lock();
							auto state = data->state;
						data->unlock();

						state.write(out);
						fflush(out);
						break;
					}
					case 'i': // image
					{
						char i;
						fscanf(in, " %c", &i);
						switch (i)
						{
							case 'f': // front
							{
								data->lock();
									auto img = data->image.at(I_FRONT);
								data->unlock();

								imageWrite(out, img);
								fflush(out);
								break;
							}
							case 'd': // down
							{
								data->lock();
									auto img = data->image.at(I_DOWN);
								data->unlock();

								imageWrite(out, img);
								fflush(out);
								break;
							}
						}
						break;
					}
					case 'm': // modeling
					{
						char m;
						fscanf(in, " %c", &m);
						switch (m)
						{
							case 'm': // mode
							{
								data->lock();
									auto model = data->model;
								data->unlock();

								model.write(out);
								fflush(out);
								break;
							}
							case 's': // system
							{
								System().write(out); // currently unsupported
								fflush(out);
								break;
							}
							case 'c': // certainty
							{
								fprintf(out, "0\n"); // currently unsupported
								fflush(out);
								break;
							}
						}
						break;
					}
				}
				break;
			}
			case 'c': // command
			{
				char c;
				fscanf(in, " %c", &c);
				switch(c)
				{
					case 's': // state
					{
						auto state = State(in);
						data->lock();
							data->desiredState = std::move(state);
							data->setState = true;
						data->unlock();
						break;
					}
					case 'd': // dropper
						data->lock();
							data->drop = true;
						data->unlock();
						break;
					case 'g': // grabber
					{
						char g;
						fscanf(in, " %c", &g);
						switch (g)
						{
							case 'g': // grab
								data->lock();
									data->grab = true;
								data->unlock();
								break;
							case 'r': // release
								data->lock();
									data->release = true;
								data->unlock();
								break;
						}
						break;
					}
					case 't': // torpedo
					{
						char t;
						fscanf(in, " %c", &t);
						data->lock();
							data->shoot = t;
						data->unlock();
						break;
					}
				}
				break;
			}
		}

		std::this_thread::sleep_for(std::chrono::milliseconds(100));
	}

	return true;
}

bool modeling(Data* data, const std::string in_name, const std::string out_name)
{
	FILE* in = openStream(in_name, "r");
	FILE* out = openStream(out_name, "w");

	// initialize model
	fprintf(out, "s\n");
	System(16, {{1, initialModel(), initialVariance()}}).write(out);
	fflush(out);

	bool quit = false;
	while (!quit)
	{
		fprintf(out, "m\n"); // request model
		fflush(out);
		auto model = Matrix(in); // read model

		std::queue<Evidence> evidence;
		// store model and get new evidences
		data->lock();
			data->model = std::move(model);
			data->modelID++;
			std::swap(evidence, data->evidence);
		data->unlock();

		// send new evidence
		while (!evidence.empty())
		{
			fprintf(out, "e\n");
			evidence.front().write(out);
			evidence.pop();
		}
		fflush(out);

		std::this_thread::sleep_for(std::chrono::milliseconds(100));
	}

	return true;
}

bool control(Data* data, const std::string in_name, const std::string out_name)
{
	FILE* in = openStream(in_name, "r");
	FILE* out = openStream(out_name, "w");

	bool quit = false;
	while (!quit)
	{
		fprintf(out, "c\n"); // request state
		fflush(out);
		auto state = State(in); // read state

		State desiredState;
		bool setState, drop, grab, release;
		char shoot;
		// store state and get new commands
		data->lock();
			data->state = std::move(state);
			data->stateID++;

			std::swap(desiredState, data->desiredState);
			setState = data->setState;
			data->setState = false;
			shoot = data->shoot;
			data->shoot = false;
			drop = data->drop;
			data->drop = false;
			grab = data->grab;
			data->grab = false;
			release = data->release;
			data->release = false;
		data->unlock();

		// send commands to arduino
		if (setState)
		{
			fprintf(out, "s\n");
			desiredState.write(out);
			fflush(out);
		}

		if (shoot == 'r') fprintf(out, "t r\n");
		if (shoot == 'l') fprintf(out, "t l\n");

		if (drop) fprintf(out, "d\n");
		if (grab) fprintf(out, "g\n");
		if (release) fprintf(out, "r\n");

		fflush(out);

		std::this_thread::sleep_for(std::chrono::milliseconds(100));
	}

	return true;
}

