// JSON (de)serialisation of library model objects, shared by the queue file, the metadata cache and downloads.
#pragma once
#include <string>
#include <vector>
#include "../subsonic/model.h"

std::string to_json(const Song& s);
std::string to_json(const Album& a);               // includes songs
std::string to_json(const Artist& a);              // includes albums (without songs)
std::string to_json(const std::vector<Album>& v);
std::string to_json(const std::vector<Artist>& v);
bool from_json(const std::string& j, Song& out);
bool from_json(const std::string& j, Album& out);
bool from_json(const std::string& j, Artist& out);
bool from_json(const std::string& j, std::vector<Album>& out);
bool from_json(const std::string& j, std::vector<Artist>& out);
