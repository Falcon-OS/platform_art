/*
 * Copyright (C) 2016 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <gtest/gtest.h>

#include "base/unix_file/fd_file.h"
#include "common_runtime_test.h"
#include "exec_utils.h"
#include "profile_assistant.h"
#include "jit/profile_compilation_info.h"
#include "utils.h"

namespace art {

class ProfileAssistantTest : public CommonRuntimeTest {
 protected:
  void SetupProfile(const std::string& id,
                    uint32_t checksum,
                    uint16_t number_of_methods,
                    uint16_t number_of_classes,
                    const ScratchFile& profile,
                    ProfileCompilationInfo* info,
                    uint16_t start_method_index = 0) {
    std::string dex_location1 = "location1" + id;
    uint32_t dex_location_checksum1 = checksum;
    std::string dex_location2 = "location2" + id;
    uint32_t dex_location_checksum2 = 10 * checksum;
    for (uint16_t i = start_method_index; i < start_method_index + number_of_methods; i++) {
      ASSERT_TRUE(info->AddMethodIndex(dex_location1, dex_location_checksum1, i));
      ASSERT_TRUE(info->AddMethodIndex(dex_location2, dex_location_checksum2, i));
    }
    for (uint16_t i = 0; i < number_of_classes; i++) {
      ASSERT_TRUE(info->AddClassIndex(dex_location1, dex_location_checksum1, dex::TypeIndex(i)));
    }

    ASSERT_TRUE(info->Save(GetFd(profile)));
    ASSERT_EQ(0, profile.GetFile()->Flush());
    ASSERT_TRUE(profile.GetFile()->ResetOffset());
  }

  int GetFd(const ScratchFile& file) const {
    return static_cast<int>(file.GetFd());
  }

  void CheckProfileInfo(ScratchFile& file, const ProfileCompilationInfo& info) {
    ProfileCompilationInfo file_info;
    ASSERT_TRUE(file.GetFile()->ResetOffset());
    ASSERT_TRUE(file_info.Load(GetFd(file)));
    ASSERT_TRUE(file_info.Equals(info));
  }

  std::string GetProfmanCmd() {
    std::string file_path = GetTestAndroidRoot();
    file_path += "/bin/profman";
    if (kIsDebugBuild) {
      file_path += "d";
    }
    EXPECT_TRUE(OS::FileExists(file_path.c_str()))
        << file_path << " should be a valid file path";
    return file_path;
  }
  // Runs test with given arguments.
  int ProcessProfiles(const std::vector<int>& profiles_fd, int reference_profile_fd) {
    std::string profman_cmd = GetProfmanCmd();
    std::vector<std::string> argv_str;
    argv_str.push_back(profman_cmd);
    for (size_t k = 0; k < profiles_fd.size(); k++) {
      argv_str.push_back("--profile-file-fd=" + std::to_string(profiles_fd[k]));
    }
    argv_str.push_back("--reference-profile-file-fd=" + std::to_string(reference_profile_fd));

    std::string error;
    return ExecAndReturnCode(argv_str, &error);
  }

  bool GenerateTestProfile(const std::string& filename) {
    std::string profman_cmd = GetProfmanCmd();
    std::vector<std::string> argv_str;
    argv_str.push_back(profman_cmd);
    argv_str.push_back("--generate-test-profile=" + filename);
    std::string error;
    return ExecAndReturnCode(argv_str, &error);
  }

  bool CreateProfile(std::string class_file_contents, const std::string& filename) {
    ScratchFile class_names_file;
    File* file = class_names_file.GetFile();
    EXPECT_TRUE(file->WriteFully(class_file_contents.c_str(), class_file_contents.length()));
    EXPECT_EQ(0, file->Flush());
    EXPECT_TRUE(file->ResetOffset());
    std::string profman_cmd = GetProfmanCmd();
    std::vector<std::string> argv_str;
    argv_str.push_back(profman_cmd);
    argv_str.push_back("--create-profile-from=" + class_names_file.GetFilename());
    argv_str.push_back("--reference-profile-file=" + filename);
    argv_str.push_back("--apk=" + GetLibCoreDexFileNames()[0]);
    argv_str.push_back("--dex-location=classes.dex");
    std::string error;
    EXPECT_EQ(ExecAndReturnCode(argv_str, &error), 0);
    return true;
  }

  bool DumpClasses(const std::string& filename, std::string* file_contents) {
    ScratchFile class_names_file;
    std::string profman_cmd = GetProfmanCmd();
    std::vector<std::string> argv_str;
    argv_str.push_back(profman_cmd);
    argv_str.push_back("--dump-classes");
    argv_str.push_back("--profile-file=" + filename);
    argv_str.push_back("--apk=" + GetLibCoreDexFileNames()[0]);
    argv_str.push_back("--dex-location=classes.dex");
    argv_str.push_back("--dump-output-to-fd=" + std::to_string(GetFd(class_names_file)));
    std::string error;
    EXPECT_EQ(ExecAndReturnCode(argv_str, &error), 0);
    File* file = class_names_file.GetFile();
    EXPECT_EQ(0, file->Flush());
    EXPECT_TRUE(file->ResetOffset());
    int64_t length = file->GetLength();
    std::unique_ptr<char[]> buf(new char[length]);
    EXPECT_EQ(file->Read(buf.get(), length, 0), length);
    *file_contents = std::string(buf.get(), length);
    return true;
  }

  bool CreateAndDump(const std::string& input_file_contents, std::string* output_file_contents) {
    ScratchFile profile_file;
    EXPECT_TRUE(CreateProfile(input_file_contents, profile_file.GetFilename()));
    profile_file.GetFile()->ResetOffset();
    EXPECT_TRUE(DumpClasses(profile_file.GetFilename(), output_file_contents));
    return true;
  }
};

TEST_F(ProfileAssistantTest, AdviseCompilationEmptyReferences) {
  ScratchFile profile1;
  ScratchFile profile2;
  ScratchFile reference_profile;

  std::vector<int> profile_fds({
      GetFd(profile1),
      GetFd(profile2)});
  int reference_profile_fd = GetFd(reference_profile);

  const uint16_t kNumberOfMethodsToEnableCompilation = 100;
  ProfileCompilationInfo info1;
  SetupProfile("p1", 1, kNumberOfMethodsToEnableCompilation, 0, profile1, &info1);
  ProfileCompilationInfo info2;
  SetupProfile("p2", 2, kNumberOfMethodsToEnableCompilation, 0, profile2, &info2);

  // We should advise compilation.
  ASSERT_EQ(ProfileAssistant::kCompile,
            ProcessProfiles(profile_fds, reference_profile_fd));
  // The resulting compilation info must be equal to the merge of the inputs.
  ProfileCompilationInfo result;
  ASSERT_TRUE(reference_profile.GetFile()->ResetOffset());
  ASSERT_TRUE(result.Load(reference_profile_fd));

  ProfileCompilationInfo expected;
  ASSERT_TRUE(expected.MergeWith(info1));
  ASSERT_TRUE(expected.MergeWith(info2));
  ASSERT_TRUE(expected.Equals(result));

  // The information from profiles must remain the same.
  CheckProfileInfo(profile1, info1);
  CheckProfileInfo(profile2, info2);
}

// TODO(calin): Add more tests for classes.
TEST_F(ProfileAssistantTest, AdviseCompilationEmptyReferencesBecauseOfClasses) {
  ScratchFile profile1;
  ScratchFile reference_profile;

  std::vector<int> profile_fds({
      GetFd(profile1)});
  int reference_profile_fd = GetFd(reference_profile);

  const uint16_t kNumberOfClassesToEnableCompilation = 100;
  ProfileCompilationInfo info1;
  SetupProfile("p1", 1, 0, kNumberOfClassesToEnableCompilation, profile1, &info1);

  // We should advise compilation.
  ASSERT_EQ(ProfileAssistant::kCompile,
            ProcessProfiles(profile_fds, reference_profile_fd));
  // The resulting compilation info must be equal to the merge of the inputs.
  ProfileCompilationInfo result;
  ASSERT_TRUE(reference_profile.GetFile()->ResetOffset());
  ASSERT_TRUE(result.Load(reference_profile_fd));

  ProfileCompilationInfo expected;
  ASSERT_TRUE(expected.MergeWith(info1));
  ASSERT_TRUE(expected.Equals(result));

  // The information from profiles must remain the same.
  CheckProfileInfo(profile1, info1);
}

TEST_F(ProfileAssistantTest, AdviseCompilationNonEmptyReferences) {
  ScratchFile profile1;
  ScratchFile profile2;
  ScratchFile reference_profile;

  std::vector<int> profile_fds({
      GetFd(profile1),
      GetFd(profile2)});
  int reference_profile_fd = GetFd(reference_profile);

  // The new profile info will contain the methods with indices 0-100.
  const uint16_t kNumberOfMethodsToEnableCompilation = 100;
  ProfileCompilationInfo info1;
  SetupProfile("p1", 1, kNumberOfMethodsToEnableCompilation, 0, profile1, &info1);
  ProfileCompilationInfo info2;
  SetupProfile("p2", 2, kNumberOfMethodsToEnableCompilation, 0, profile2, &info2);


  // The reference profile info will contain the methods with indices 50-150.
  const uint16_t kNumberOfMethodsAlreadyCompiled = 100;
  ProfileCompilationInfo reference_info;
  SetupProfile("p1", 1, kNumberOfMethodsAlreadyCompiled, 0, reference_profile,
      &reference_info, kNumberOfMethodsToEnableCompilation / 2);

  // We should advise compilation.
  ASSERT_EQ(ProfileAssistant::kCompile,
            ProcessProfiles(profile_fds, reference_profile_fd));

  // The resulting compilation info must be equal to the merge of the inputs
  ProfileCompilationInfo result;
  ASSERT_TRUE(reference_profile.GetFile()->ResetOffset());
  ASSERT_TRUE(result.Load(reference_profile_fd));

  ProfileCompilationInfo expected;
  ASSERT_TRUE(expected.MergeWith(info1));
  ASSERT_TRUE(expected.MergeWith(info2));
  ASSERT_TRUE(expected.MergeWith(reference_info));
  ASSERT_TRUE(expected.Equals(result));

  // The information from profiles must remain the same.
  CheckProfileInfo(profile1, info1);
  CheckProfileInfo(profile2, info2);
}

TEST_F(ProfileAssistantTest, DoNotAdviseCompilation) {
  ScratchFile profile1;
  ScratchFile profile2;
  ScratchFile reference_profile;

  std::vector<int> profile_fds({
      GetFd(profile1),
      GetFd(profile2)});
  int reference_profile_fd = GetFd(reference_profile);

  const uint16_t kNumberOfMethodsToSkipCompilation = 1;
  ProfileCompilationInfo info1;
  SetupProfile("p1", 1, kNumberOfMethodsToSkipCompilation, 0, profile1, &info1);
  ProfileCompilationInfo info2;
  SetupProfile("p2", 2, kNumberOfMethodsToSkipCompilation, 0, profile2, &info2);

  // We should not advise compilation.
  ASSERT_EQ(ProfileAssistant::kSkipCompilation,
            ProcessProfiles(profile_fds, reference_profile_fd));

  // The information from profiles must remain the same.
  ProfileCompilationInfo file_info1;
  ASSERT_TRUE(profile1.GetFile()->ResetOffset());
  ASSERT_TRUE(file_info1.Load(GetFd(profile1)));
  ASSERT_TRUE(file_info1.Equals(info1));

  ProfileCompilationInfo file_info2;
  ASSERT_TRUE(profile2.GetFile()->ResetOffset());
  ASSERT_TRUE(file_info2.Load(GetFd(profile2)));
  ASSERT_TRUE(file_info2.Equals(info2));

  // Reference profile files must remain empty.
  ASSERT_EQ(0, reference_profile.GetFile()->GetLength());

  // The information from profiles must remain the same.
  CheckProfileInfo(profile1, info1);
  CheckProfileInfo(profile2, info2);
}

TEST_F(ProfileAssistantTest, FailProcessingBecauseOfProfiles) {
  ScratchFile profile1;
  ScratchFile profile2;
  ScratchFile reference_profile;

  std::vector<int> profile_fds({
      GetFd(profile1),
      GetFd(profile2)});
  int reference_profile_fd = GetFd(reference_profile);

  const uint16_t kNumberOfMethodsToEnableCompilation = 100;
  // Assign different hashes for the same dex file. This will make merging of information to fail.
  ProfileCompilationInfo info1;
  SetupProfile("p1", 1, kNumberOfMethodsToEnableCompilation, 0, profile1, &info1);
  ProfileCompilationInfo info2;
  SetupProfile("p1", 2, kNumberOfMethodsToEnableCompilation, 0, profile2, &info2);

  // We should fail processing.
  ASSERT_EQ(ProfileAssistant::kErrorBadProfiles,
            ProcessProfiles(profile_fds, reference_profile_fd));

  // The information from profiles must remain the same.
  CheckProfileInfo(profile1, info1);
  CheckProfileInfo(profile2, info2);

  // Reference profile files must still remain empty.
  ASSERT_EQ(0, reference_profile.GetFile()->GetLength());
}

TEST_F(ProfileAssistantTest, FailProcessingBecauseOfReferenceProfiles) {
  ScratchFile profile1;
  ScratchFile reference_profile;

  std::vector<int> profile_fds({
      GetFd(profile1)});
  int reference_profile_fd = GetFd(reference_profile);

  const uint16_t kNumberOfMethodsToEnableCompilation = 100;
  // Assign different hashes for the same dex file. This will make merging of information to fail.
  ProfileCompilationInfo info1;
  SetupProfile("p1", 1, kNumberOfMethodsToEnableCompilation, 0, profile1, &info1);
  ProfileCompilationInfo reference_info;
  SetupProfile("p1", 2, kNumberOfMethodsToEnableCompilation, 0, reference_profile, &reference_info);

  // We should not advise compilation.
  ASSERT_TRUE(profile1.GetFile()->ResetOffset());
  ASSERT_TRUE(reference_profile.GetFile()->ResetOffset());
  ASSERT_EQ(ProfileAssistant::kErrorBadProfiles,
            ProcessProfiles(profile_fds, reference_profile_fd));

  // The information from profiles must remain the same.
  CheckProfileInfo(profile1, info1);
}

TEST_F(ProfileAssistantTest, TestProfileGeneration) {
  ScratchFile profile;
  // Generate a test profile.
  GenerateTestProfile(profile.GetFilename());

  // Verify that the generated profile is valid and can be loaded.
  ASSERT_TRUE(profile.GetFile()->ResetOffset());
  ProfileCompilationInfo info;
  ASSERT_TRUE(info.Load(GetFd(profile)));
}

TEST_F(ProfileAssistantTest, TestProfileCreationAllMatch) {
  // Class names put here need to be in sorted order.
  std::vector<std::string> class_names = {
    "java.lang.Comparable",
    "java.lang.Math",
    "java.lang.Object"
  };
  std::string input_file_contents;
  for (std::string& class_name : class_names) {
    input_file_contents += class_name + std::string("\n");
  }
  std::string output_file_contents;
  ASSERT_TRUE(CreateAndDump(input_file_contents, &output_file_contents));
  ASSERT_EQ(output_file_contents, input_file_contents);
}

TEST_F(ProfileAssistantTest, TestProfileCreationOneNotMatched) {
  // Class names put here need to be in sorted order.
  std::vector<std::string> class_names = {
    "doesnt.match.this.one",
    "java.lang.Comparable",
    "java.lang.Object"
  };
  std::string input_file_contents;
  for (std::string& class_name : class_names) {
    input_file_contents += class_name + std::string("\n");
  }
  std::string output_file_contents;
  ASSERT_TRUE(CreateAndDump(input_file_contents, &output_file_contents));
  std::string expected_contents =
      class_names[1] + std::string("\n") + class_names[2] + std::string("\n");
  ASSERT_EQ(output_file_contents, expected_contents);
}

TEST_F(ProfileAssistantTest, TestProfileCreationNoneMatched) {
  // Class names put here need to be in sorted order.
  std::vector<std::string> class_names = {
    "doesnt.match.this.one",
    "doesnt.match.this.one.either",
    "nor.this.one"
  };
  std::string input_file_contents;
  for (std::string& class_name : class_names) {
    input_file_contents += class_name + std::string("\n");
  }
  std::string output_file_contents;
  ASSERT_TRUE(CreateAndDump(input_file_contents, &output_file_contents));
  std::string expected_contents("");
  ASSERT_EQ(output_file_contents, expected_contents);
}

}  // namespace art
