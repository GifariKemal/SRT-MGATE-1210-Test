#ifndef MEMORY_MANAGER_H
#define MEMORY_MANAGER_H

#include <memory>
#include <esp_heap_caps.h>
#include <new>  // Required for placement new

/*
 * @brief Custom deleter for std::unique_ptr to free PSRAM memory.
 * 
 * This struct provides a function call operator that will be invoked by
 * std::unique_ptr when the managed object goes out of scope. It calls
 * heap_caps_free, ensuring PSRAM-allocated memory is correctly released.
 */
struct PsramDeleter {
  void operator()(void* ptr) const {
    if (ptr) {
      // First, manually call the destructor of the object.
      // This is crucial for objects created with placement new.
      // We assume the type is known at deletion time, but for a generic
      // deleter, we can only free the memory. The destructor should be
      // called before the unique_ptr goes out of scope if type is known.
      // For simplicity in this generic deleter, we just free.
      // The improved make_psram_unique will handle typed destruction.
      heap_caps_free(ptr);
    }
  }
};

// Custom deleter for a specific type T
template<typename T>
struct PsramTypeDeleter {
  void operator()(T* ptr) const {
    if (ptr) {
      ptr->~T();            // Manually call the destructor
      heap_caps_free(ptr);  // Free the memory
    }
  }
};


/*
 * @brief Alias for a unique_ptr that manages a PSRAM-allocated object.
 */
template<typename T>
using PsramUniquePtr = std::unique_ptr<T, PsramTypeDeleter<T>>;

/*
 * @brief Factory function to create an object in PSRAM and wrap it in a PsramUniquePtr.
 *
 * This function allocates memory in PSRAM, constructs an object of type T
 * in that memory (placement new), and returns a smart pointer that will

 * automatically handle destruction and deallocation.
 *
 * @tparam T The type of object to create.
 * @tparam Args The types of arguments for the constructor of T.
 * @param args The arguments to forward to the constructor of T.
 * @return A PsramUniquePtr managing the newly created object, or nullptr on failure.
 */
template<typename T, typename... Args>
PsramUniquePtr<T> make_psram_unique(Args&&... args) {
  void* mem = heap_caps_malloc(sizeof(T), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!mem) {
    return nullptr;  // Allocation failed
  }
  // Construct the object in the allocated memory and return a smart pointer
  return PsramUniquePtr<T>(new (mem) T(std::forward<Args>(args)...));
}

#endif  // MEMORY_MANAGER_H
