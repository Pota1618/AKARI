#include <glm/vec3.hpp>

#include <iostream>

int main()
{
    constexpr glm::vec3 vector{1.0F, 2.0F, 3.0F};

    std::cout << "GLM vector: " << vector.x << ", " << vector.y << ", " << vector.z << '\n';
    return 0;
}
