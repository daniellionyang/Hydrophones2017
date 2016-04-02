#include "mission/action.hpp"

#include "mission/command.hpp"
#include "mission/query.hpp"

MoveTo::MoveTo(const Matrix& target, float minDistance) :
	target(target),
	minDistance(minDistance)
{
}

bool MoveTo::run(FILE* in, FILE* out)
{
	bool close = false;
	while (!close)
	{
		Matrix state = getState(in, out);
		Matrix location; // get subarray of state (unimplemented)

		move(out, location, target);

		if ((target - location).magnitude() < minDistance)
			close = true;
	}
	return true;
}

