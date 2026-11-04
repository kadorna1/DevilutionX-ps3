#pragma once

#ifdef __PS3__

namespace devilution {

bool PS3VdecMovieBegin(const char *filename, int flags);
bool PS3VdecMovieContinue();
void PS3VdecMovieEnd();

} // namespace devilution

#endif
