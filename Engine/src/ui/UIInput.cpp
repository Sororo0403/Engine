#include "ui/UIInput.h"
#include <cmath>

bool UIInput::IsValid() const {
	return width >= 1 && width <= 16384 && height >= 1 && height <= 16384 &&
	       horizontal >= -1 && horizontal <= 1 && vertical >= -1 &&
	       vertical <= 1 &&
	       (!pointerValid ||
	        (std::isfinite(pointerX) && std::isfinite(pointerY) &&
	         pointerX >= 0 && pointerX <= 1 && pointerY >= 0 && pointerY <= 1));
}
