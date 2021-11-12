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
#include <set>

namespace fs = std::filesystem;

// Class to hold RGB values
class RGB {
public:
    int r = -1;
    int g = -1;
    int b = -1;
};

// Class to hold HSL values
class HSL {
public:
    double h = -1;
    double s = -1;
    int l = -1;
};

// Class to hold relative image values
class Image {
public:
    std::string fileName;

    std::vector<RGB> rgb;
    RGB averageRgb;
    HSL hsl;
};

// Struct to hold the Image objects at a specified point in the pipeline
struct pile_t {

    // Remove and return last item
    Image Pop() {
        std::lock_guard<std::mutex> guard(mutex);
        Image work_item;

        if (!data.empty()) {
            work_item = data.back();
            data.pop_back();
        }

        return work_item;
    }

    // Put item into vector
    void Put(Image work_item) {
        std::lock_guard<std::mutex> guard(mutex);
        data.push_back(work_item);
    }

    // Return size of vector
    int Num() {
        std::lock_guard<std::mutex> guard(mutex);
        return int(data.size());
    }

    // Return tge data vector
    std::vector<Image>& GetData() {
        std::lock_guard<std::mutex> guard(mutex);
        return data;
    }

private:
    // Mutex to lock to a thread
    std::mutex mutex;
    // Data vector
    std::vector<Image> data;
};

// Custom compare lambda
struct image_cmp {
    bool operator()(const Image& a, const Image& b) const {
        return a.hsl.h < b.hsl.h;
    }
};

constexpr char* image_folder = "par_images/unsorted";
std::set<Image, image_cmp> sortedImages;
int imageCount = 999999;

pile_t to_get_pixels;
pile_t to_get_average_color;
pile_t to_convert_rgb_to_hsl;
pile_t done;

sf::Vector2f ScaleFromDimensions(const sf::Vector2u& textureSize, int screenWidth, int screenHeight)
{
    float scaleX = screenWidth / float(textureSize.x);
    float scaleY = screenHeight / float(textureSize.y);
    float scale = std::min(scaleX, scaleY);
    return { scale, scale };
}

// Load all image filenames and add them to the beginning of the pipeline
void LoadImages()
{    
    imageCount = 0;
    for (auto& p : fs::directory_iterator(image_folder))
    {
        Image img;
        img.fileName = p.path().u8string();

        to_get_pixels.Put(img);
        imageCount++;
    }
}

// Load image based on object, gather all pixels RGB values storing them in RGB object and add it to the object.
void GetPixels(Image &img) {
    sf::Texture texture;
    if (!texture.loadFromFile(img.fileName)) {
        std::cout << "Failed" << std::endl;
        return;
    }
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



// Sort passed list with custom compare function SortByHue()
//void SortList() {
//    auto img = done.GetData();
//    std::sort(img.begin(), img.end(), SortByHue);
//}

// Get the Average RGB value from a list of RGBs
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

// Convert RGB to HSL
void RgbToHsl(Image &img) {
    HSL hsl;

    double r = img.averageRgb.r / 255.f;
    double g = img.averageRgb.g / 255.f;
    double b = img.averageRgb.b / 255.f;

    double max = std::max(std::max(r, g), b);
    double min = std::min(std::min(r, g), b);

    double delta = max - min;

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

// Driver function for GetPixels(), constantly running on seperate thread
// Get image from start of pipeline, get it's pixels then add it to the next section of the pipeline
void GetPixelsDriver() {
    bool loop = true;

    while (loop) {
        if (to_get_pixels.Num() > 0) {
            Image img = to_get_pixels.Pop();
            //std::cout << "Calculating image pixels: " << img.fileName << std::endl;
            GetPixels(img);
            to_get_average_color.Put(img);
        }

        // This prevents this loop ending before imageCount is initially updated.
        if (sortedImages.size() == imageCount && imageCount > 0) {
            loop = false;
        }
    }
}

// Driver function for AverageColour(), constantly running on seperate thread
// Get image from respective part of pipeline, get it's average colour then add it to the next section of the pipeline
void AverageColourDriver() {
    bool loop = true;

    while (loop) {
        if (to_get_average_color.Num() > 0) {
            Image img = to_get_average_color.Pop();
            //std::cout << "Calculating image average colour: " << img.fileName << std::endl;
            AverageRgbColour(img);
            to_convert_rgb_to_hsl.Put(img);
        }

        // This prevents this loop ending before imageCount is initially updated.
        if (sortedImages.size() == imageCount && imageCount > 0) {
            loop = false;
        }
    }
}

// Driver function for RgbToHsl(), constantly running on seperate thread
// Get image from respective part of pipeline, convert its colour values from RGB to HSL then add it to the next section of the pipeline
void RgbToHslDriver() {
    bool loop = true;

    while (loop) {
        if (to_convert_rgb_to_hsl.Num() > 0) {
            Image img = to_convert_rgb_to_hsl.Pop();
            //std::cout << "converting image pixels to hsl: " << img.fileName << std::endl;
            RgbToHsl(img);
            done.Put(img);
        }

        // This prevents this loop ending before imageCount is initially updated.
        if (sortedImages.size() == imageCount && imageCount > 0) {
            loop = false;
        }
    }
}

// Driver function for SortList(), constantly running on seperate thread
// Get image vector from respective part of pipeline, sort it then add it to the next section of the pipeline
void SortDriver() {
    bool loop = true;
        while (loop) {
            if (done.Num() > 0) {
                auto img = done.Pop();
                sortedImages.insert(img);
                std::cout << "First item sorted" << std::endl;
            }

            if (sortedImages.size() == imageCount && imageCount > 0)
                loop = false;
        }
}

// For Debugging, Print values and filename
// Can run multithread or single thread
void PrintWhenComplete() {
    bool loop = true;    

    while (loop) {
        if (sortedImages.size() == imageCount) {
            std::vector<Image> Images;
            std::copy(sortedImages.begin(), sortedImages.end(), std::back_inserter(Images));

            std::cout << std::endl;

            for (auto img : Images) {
                std::cout << img.fileName << "\t | \t" << img.hsl.h << std::endl;
            }

            loop = false; 
        }
    }
} 

int main()
{
    std::srand(static_cast<unsigned int>(std::time(NULL)));
    //std::cout << fs::current_path();

    // Define some constants
    const float pi = 3.14159f;
    const int gameWidth = 800;
    const int gameHeight = 600;

    int imageIndex = 0;

    // Create the window of the application
    sf::RenderWindow window(sf::VideoMode(gameWidth, gameHeight, 32), "Image Fever",
                            sf::Style::Titlebar | sf::Style::Close);
    window.setVerticalSyncEnabled(true);      

    // Create SFML objects to display the images
    sf::Texture texture;
    sf::Sprite sprite;
    
    // This is used to also output values when complete
    // std::array<std::thread, 6> threads = { std::thread(LoadImages), std::thread(GetPixelsDriver), std::thread(AverageColourDriver), std::thread(RgbToHslDriver), std::thread(SortDriver), std::thread(PrintWhenComplete) };

    // This is used when you don't want to output values for performance measurement
    std::array<std::thread, 5> threads = { std::thread(LoadImages),  std::thread(GetPixelsDriver), std::thread(AverageColourDriver), std::thread(RgbToHslDriver), std::thread(SortDriver) };

    for (auto& t : threads)
        t.detach();
    
    // If there is no texture and a image that has been completely processed
    // loop until one has been processed then set the image
    while (true) {
        if (sprite.getTexture() == nullptr && sortedImages.size() > 0)
        {
            std::vector<Image> Images;
            std::copy(sortedImages.begin(), sortedImages.end(), std::back_inserter(Images));
            if (texture.loadFromFile(Images[0].fileName))
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
        std::vector<Image> Images;

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
                    imageIndex = (imageIndex + sortedImages.size() - 1) % sortedImages.size();
                else if (event.key.code == sf::Keyboard::Key::Right)
                    imageIndex = (imageIndex + 1) % sortedImages.size();
                // get image filename
                std::copy(sortedImages.begin(), sortedImages.end(), std::back_inserter(Images));
                if (Images.size() > 0)
                {
                    auto& imageFilename = Images[imageIndex].fileName;
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


