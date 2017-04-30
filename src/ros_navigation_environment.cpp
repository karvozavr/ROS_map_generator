#include "ros_navigation_environment.h"

RosNavigationEnvironment::RosNavigationEnvironment(size_t amount,
                                                   int64_t min_size,
                                                   int64_t max_size,
                                                   int64_t corridor_width,
                                                   size_t width,
                                                   size_t height,
                                                   Randomizer<int64_t> *random,
                                                   bool obstacles)
    : width_(width), height_(height), corridor_width_(corridor_width), random_(random) {

  for (int i = 0; i < amount; ++i) {
    rooms_.push_back(Room(random_,
                          obstacles ? corridor_width : 0,
                          static_cast<int64_t>(width / 2),
                          static_cast<int64_t>(height / 2),
                          std::abs(random_->next_rand()) % max_size + min_size,
                          std::abs(random_->next_rand()) % max_size + min_size
    ));
  }

  separateRooms(0);
  removeOverboundingRooms();
  createRNG();
  connectRooms();

  std::reverse(rooms_.begin(), rooms_.end());
}

void RosNavigationEnvironment::separateRooms(int64_t padding) {

  // holds delta values of the overlap
  int64_t delta_x;
  int64_t delta_x_a;
  int64_t delta_x_b;

  int64_t delta_y;
  int64_t delta_y_a;
  int64_t delta_y_b;

  // a bool flag to keep track of touching halls
  bool touching;

  do {
    touching = false;

    for (size_t i = 0; i < rooms_.size(); i++) {
      Room &room_a = rooms_.at(i);
      for (size_t j = i + 1; j < rooms_.size(); j++) { // for each pair of rooms
        Room &room_b = rooms_.at(j);

        if (room_a.touches(room_b, padding)) { // if the two halls touch (allowed to overlap by 1)
          touching = true; // update the touching flag so the loop iterates again

          // find the two smallest deltas required to stop the overlap
          delta_x = std::min(room_a.getRight() - room_b.getLeft() + padding,
                             room_a.getLeft() - room_b.getRight() - padding);
          delta_y = std::min(room_a.getBottom() - room_b.getTop() + padding,
                             room_a.getTop() - room_b.getBottom() - padding);

          // only keep the smalled delta
          if (std::abs(delta_x) < std::abs(delta_y)) {
            delta_y = 0;
          } else {
            delta_x = 0;
          }

          // create a delta for each rectangle as half the whole delta.
          delta_x_a = -delta_x / 2;
          delta_x_b = delta_x + delta_x_a;

          // same for y
          delta_y_a = -delta_y / 2;
          delta_y_b = delta_y + delta_y_a;

          // shift both rectangles
          room_a.relocate(delta_x_a, delta_y_a);
          room_b.relocate(delta_x_b, delta_y_b);
        }
      }
    }
  } while (touching); // loop until no rectangles are touching
}


void RosNavigationEnvironment::removeOverboundingRooms() {
  for (auto room = rooms_.begin(); room != rooms_.end(); ++room) {
    if (room->getTop() >= height_ ||
        room->getBottom() < 0 ||
        room->getRight() >= width_ ||
        room->getLeft() < 0) {
      rooms_.erase(room--);
    }
  }
}

void RosNavigationEnvironment::createRNG() {

  // choose the biggest halls as halls
  size_t hall_size = rooms_.size() / 2;
  std::sort(rooms_.begin(), rooms_.end(), Room::CompareBySquare());
  rooms_.resize(hall_size);
  room_vertices_.resize(hall_size);

  // associate halls and vertex descriptors
  {
    auto room = rooms_.begin();
    auto vertex = room_vertices_.begin();

    for (room, vertex; room != rooms_.end() && vertex != room_vertices_.end(); ++room, ++vertex) {
      *vertex = boost::add_vertex(room_graph_);
      room_graph_[*vertex].room = &(*room);
    }
  }

  // squared distance between each of the 3 halls
  double a_to_b_distance;
  double a_to_c_distance;
  double b_to_c_distance;

  bool skip;


  // for each pair of halls and their associated vertices

  auto vertex_a = room_vertices_.begin();
  for (auto room_a = rooms_.begin(); room_a != rooms_.end() && vertex_a != room_vertices_.end(); ++room_a, ++vertex_a) {
    auto vertex_b = vertex_a + 1;
    for (auto room_b = room_a + 1; room_b != rooms_.end() && vertex_b != room_vertices_.end(); ++room_b, ++vertex_b) {
      skip = false;

      // get the squared distance between a and b
      a_to_b_distance = pow(room_a->getCenterX() - room_b->getCenterX(), 2) +
                        pow(room_a->getCenterY() - room_b->getCenterY(), 2);

      // loop through all other halls, that are not a or b
      for (auto room_c = rooms_.begin(); room_c != rooms_.end(); ++room_c) {

        if (room_c == room_a || room_c == room_b) {
          continue;
        }

        // get the other two applicable distances
        a_to_c_distance =
            pow(room_a->getCenterX() - room_c->getCenterX(), 2) + pow(room_a->getCenterY() - room_c->getCenterY(), 2);
        b_to_c_distance =
            pow(room_b->getCenterX() - room_c->getCenterX(), 2) + pow(room_b->getCenterY() - room_c->getCenterY(), 2);


        // if the distance between a and c or b and c are smaller than a,
        // the pair (a, b) is not a graph edge
        if (a_to_c_distance < a_to_b_distance && b_to_c_distance < a_to_b_distance) {
          skip = true;
        }

        // break the loop and go to the next (a, b) pair
        if (skip) {
          break;
        }
      }

      // if this (a, b) pair was never skipped, it should be an edge
      if (!skip) {
        if (vertex_a != vertex_b)
          boost::add_edge(*vertex_a, *vertex_b, room_graph_);
      }
    }
  }
}

void RosNavigationEnvironment::connectRooms() {

  int64_t delta_x;
  int64_t delta_y;
  int64_t a_x;
  int64_t a_y;
  int64_t b_x;
  int64_t b_y;

  const Room *room_a;
  const Room *room_b;

  // iterate trough all edges
  auto vertices = boost::vertices(room_graph_);
  for (auto vertex_it = vertices.first; vertex_it != vertices.second; ++vertex_it) {
    auto neighbors = boost::adjacent_vertices(*vertex_it, room_graph_);
    for (auto neighbors_it = neighbors.first; neighbors_it != neighbors.second; ++neighbors_it) {

      // ensure A is never to the right of B;
      if (room_graph_[*vertex_it].room->getCenterX() < room_graph_[*neighbors_it].room->getCenterX()) {
        room_a = room_graph_[*vertex_it].room;
        room_b = room_graph_[*neighbors_it].room;
      } else {
        room_a = room_graph_[*neighbors_it].room;
        room_b = room_graph_[*vertex_it].room;
      }

      // the starting points
      a_x = static_cast<int64_t>(room_a->getCenterX());
      a_y = static_cast<int64_t>(room_a->getCenterY());

      b_x = static_cast<int64_t>(room_b->getCenterX());
      b_y = static_cast<int64_t>(room_b->getCenterY());

      assert(a_x <= b_x);

      // the deltas from A to B
      delta_x = static_cast<int64_t>(b_x - a_x);
      delta_y = static_cast<int64_t>(b_y - a_y);


      // randomly bend clockwise or counter clockwise
      if (random_->next_rand() % 2) {
        rooms_.push_back(Room(a_x, a_y, std::abs(delta_x) + corridor_width_, corridor_width_)); // horizontal
        rooms_.push_back(Room(b_x, std::min(a_y, b_y), corridor_width_, std::abs(delta_y))); // vertical
      } else {
        rooms_.push_back(Room(a_x, std::min(a_y, b_y), corridor_width_, std::abs(delta_y))); // same as above
        rooms_.push_back(Room(a_x, b_y, std::abs(delta_x), corridor_width_)); // swapped horizontal and vertical
      }
    }
  }
}

