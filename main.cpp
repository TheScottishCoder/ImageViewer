// This is a chopped Pong example from SFML examples

////////////////////////////////////////////////////////////
// Headers
////////////////////////////////////////////////////////////
#include <SFML/Graphics.hpp>
#include <cmath>
#include <ctime>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <float.h>
#include <mutex>
#include <array>
#include <thread>

namespace fs = std::filesystem;

class RGB {
public:
    int r = -1;
    int g = -1;
    int b = -1;
};

class HSL {
public:
    double h = -1;
    double s = -1;
    int l = -1;
};

class Image {
public:
    std::string fileName;

    std::vector<RGB> rgb;
    RGB averageRgb;
    HSL hsl;

    int medianHue = -1;
    double temperature = -1;
};

struct pile_t {
    Image Pop() {
        std::lock_guard<std::mutex> guard(mutex);
        Image work_item;

        if (!data.empty()) {
            work_item = data.back();
            data.pop_back();
        }

        return work_item;
    }

    void Put(Image work_item) {
        std::lock_guard<std::mutex> guard(mutex);
        data.push_back(work_item);
    }

    int Num() {
        std::lock_guard<std::mutex> guard(mutex);
        return int(data.size());
    }

    std::vector<Image> GetData() {
        std::lock_guard<std::mutex> guard(mutex);
        return data;
    }

private:
    std::mutex mutex;
    std::vector<Image> data;
};

constexpr char* image_folder = "par_images/unsorted";
std::vector<Image> sortedImages;
int imageCount;

pile_t to_get_pixels;
pile_t to_get_average_color;
pile_t to_convert_rgb_to_hsl;
//pile_t to_sort;
pile_t done;

sf::Vector2f ScaleFromDimensions(const sf::Vector2u& textureSize, int screenWidth, int screenHeight)
{
    float scaleX = screenWidth / float(textureSize.x);
    float scaleY = screenHeight / float(textureSize.y);
    float scale = std::min(scaleX, scaleY);
    return { scale, scale };
}

void LoadImages()
{    
    for (auto& p : fs::directory_iterator(image_folder))
    {
        Image img;
        img.fileName = p.path().u8string();

        to_get_pixels.Put(img);
        imageCount++;
    }
}

void GetPixels(Image &img) {
    sf::Texture texture;
    if (!texture.loadFromFile(img.fileName))
        return;
    sf::Sprite sprite(texture);

    auto image = sprite.getTexture()->copyToImage();

    for (int y = 0; y < image.getSize().y; y++) {
        for (int x = 0; x < image.getSize().x; x++) {
            RGB rgb;

            rgb.r = image.getPixel(x, y).r;
            rgb.g = image.getPixel(x, y).g;
            rgb.b = image.getPixel(x, y).b;

            img.rgb.push_back(rgb);
        }
    }
}

bool SortByHue(Image& img1, Image& img2) {
    return img1.hsl.h < img2.hsl.h;
}

void SortList(std::vector<Image> img) {
    std::sort(img.begin(), img.end(), SortByHue);
}

void AverageRgbColour(Image &img) {
    int r = 0, g = 0, b = 0;
    for (RGB c : img.rgb) {
        r += c.r;
        g += c.g;
        b += c.b;
    }

    RGB average;
    average.r = (r / img.rgb.size());
    average.g = (g / img.rgb.size());
    average.b = (b / img.rgb.size());

    img.averageRgb = average;
}

void RgbToHsl(Image &img) {
    HSL hsl;

    double r = img.averageRgb.r / 255.f;
    double g = img.averageRgb.g / 255.f;
    double b = img.averageRgb.b / 255.f;

    double max = std::max(std::max(r, g), b);
    double min = std::min(std::min(r, g), b);

    double delta = max - min;

    hsl.l = (max + min) / 2.f;

    if (max == min) {
        hsl.h = 0.f;
    }
    else {
        if (max == r) {
            double temp;
            if (g < b)
                temp = 6.f;
            else
                temp = 0.f;

            hsl.h = (g - b) / delta + temp;
        }
        else if (max == g) {
            hsl.h = (b - r) / delta + 2.f;
        }
        else if (max == b) {
            hsl.h = (r - g) / delta + 4.f;
        }
    }

    hsl.h = (hsl.h / 6) * 360;

    img.hsl = hsl;
}

void GetPixelsDriver() {
    while (done.Num() != imageCount && to_get_pixels.Num() != 0) {
        Image img = to_get_pixels.Pop();
        //std::cout << "Calculating image pixels: " << img.fileName << std::endl;
        GetPixels(img);
        to_get_average_color.Put(img);
    }
}

void AverageColourDriver() {
    while (done.Num() < imageCount) {
        if (to_get_average_color.Num() > 0) {
            Image img = to_get_average_color.Pop();
            //std::cout << "Calculating image average colour: " << img.fileName << std::endl;
            AverageRgbColour(img);
            to_convert_rgb_to_hsl.Put(img);
        }
    }
}

void RgbToHslDriver() {
    while (done.Num() != imageCount) {
        if (to_convert_rgb_to_hsl.Num() > 0) {
            Image img = to_convert_rgb_to_hsl.Pop();
            //std::cout << "converting image pixels to hsl: " << img.fileName << std::endl;
            RgbToHsl(img);
            done.Put(img);
        }
    }
}

void SortDriver() {
    while (done.Num() != imageCount) {
        if (done.Num() > 0) {
            SortList(done.GetData());
        }
    }
}

void PrintWhenComplete() {
    bool loop = true;
    while (loop) {
        //if (done.Num() > 0)
        //    std::cout << "go" << std::endl;

        if (done.Num() == imageCount) {
            //std::cout << "Please hold: " << std::endl;
            //std::this_thread::sleep_for(std::chrono::seconds(2));
            for (auto img : done.GetData()) {
                std::cout << img.fileName << "\t | \t" << img.hsl.h << std::endl;
            }

            loop = false; 
        }
    }
}

int main()
{
    std::srand(static_cast<unsigned int>(std::time(NULL)));
    std::cout << fs::current_path();

    // load all filenames
    LoadImages();

    // Define some constants
    const float pi = 3.14159f;
    const int gameWidth = 800;
    const int gameHeight = 600;

    int imageIndex = 0;

    // Create the window of the application
    sf::RenderWindow window(sf::VideoMode(gameWidth, gameHeight, 32), "Image Fever",
                            sf::Style::Titlebar | sf::Style::Close);
    window.setVerticalSyncEnabled(true);      

    sf::Texture texture;
    sf::Sprite sprite;

    std::array<std::thread, 5> threads = { std::thread(GetPixelsDriver), std::thread(AverageColourDriver), std::thread(RgbToHslDriver), std::thread(SortDriver), std::thread(PrintWhenComplete) };

    

    while (true) {
        if (sprite.getTexture() == nullptr && done.Num() > 0)
        {
            auto vec = done.GetData();

            if (texture.loadFromFile(vec[0].fileName))
            {
                sprite = sf::Sprite(texture);
                sprite.setScale(ScaleFromDimensions(texture.getSize(), gameWidth, gameHeight));
            }

            break;
        }
    }

    sf::Clock clock;
    while (window.isOpen())
    {
        auto vec = done.GetData();

        // Handle events
        sf::Event event;
        while (window.pollEvent(event))
        {
            // Window closed or escape key pressed: exit
            if ((event.type == sf::Event::Closed) ||
               ((event.type == sf::Event::KeyPressed) && (event.key.code == sf::Keyboard::Escape)))
            {
                window.close();
                break;
            }

            // Window size changed, adjust view appropriately
            if (event.type == sf::Event::Resized)
            {
                sf::View view;
                view.setSize(gameWidth, gameHeight);
                view.setCenter(gameWidth/2.f, gameHeight/2.f);
                window.setView(view);
            }

            // Arrow key handling!
            if (event.type == sf::Event::KeyPressed)
            {                           
                

                // adjust the image index
                if (event.key.code == sf::Keyboard::Key::Left)
                    imageIndex = (imageIndex + vec.size() - 1) % vec.size();
                else if (event.key.code == sf::Keyboard::Key::Right)
                    imageIndex = (imageIndex + 1) % vec.size();
                // get image filename

                std::cout << done.Num() << std::endl;
                if (done.Num() > 0)
                {
                    auto& imageFilename = vec[imageIndex].fileName;
                    // set it as the window title
                    window.setTitle(imageFilename);
                    // ... and load the appropriate texture, and put it in the sprite
                    if (texture.loadFromFile(imageFilename))
                    {
                        sprite = sf::Sprite(texture);
                        sprite.setScale(ScaleFromDimensions(texture.getSize(), gameWidth, gameHeight));
                    }
                }
            }
        }

        // Clear the window
        window.clear(sf::Color(0, 0, 0));
        // draw the sprite
        window.draw(sprite);
        // Display things on screen
        window.display();
    }

    return EXIT_SUCCESS;
}


