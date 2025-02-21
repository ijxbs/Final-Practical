//Just a simple handler for simple initialization stuffs
#include "BackendHandler.h"

#include <filesystem>
#include <json.hpp>
#include <fstream>

#include <Texture2D.h>
#include <Texture2DData.h>
#include <MeshBuilder.h>
#include <MeshFactory.h>
#include <NotObjLoader.h>
#include <ObjLoader.h>
#include <VertexTypes.h>
#include <ShaderMaterial.h>
#include <RendererComponent.h>
#include <TextureCubeMap.h>
#include <TextureCubeMapData.h>

#include <Timing.h>
#include <GameObjectTag.h>
#include <InputHelpers.h>

#include <IBehaviour.h>
#include <CameraControlBehaviour.h>
#include <FollowPathBehaviour.h>
#include <SimpleMoveBehaviour.h>


int main() {
	int frameIx = 0;
	float fpsBuffer[128];
	float minFps, maxFps, avgFps;
	int selectedVao = 0; // select cube by default
	std::vector<GameObject> controllables;

	BackendHandler::InitAll();

	// Let OpenGL know that we want debug output, and route it to our handler function
	glEnable(GL_DEBUG_OUTPUT);
	glDebugMessageCallback(BackendHandler::GlDebugMessage, nullptr);

	// Enable texturing
	glEnable(GL_TEXTURE_2D);

	// Push another scope so most memory should be freed *before* we exit the app
	{
		#pragma region Shader and ImGui

		// Load our shaders
		Shader::sptr shader = Shader::Create();
		shader->LoadShaderPartFromFile("shaders/vertex_shader.glsl", GL_VERTEX_SHADER);
		shader->LoadShaderPartFromFile("shaders/frag_blinn_phong_textured.glsl", GL_FRAGMENT_SHADER);
		shader->Link();

		Shader::sptr colorCorrectionShader = Shader::Create();
		colorCorrectionShader->LoadShaderPartFromFile("shaders/passthrough_vert.glsl", GL_VERTEX_SHADER);
		colorCorrectionShader->LoadShaderPartFromFile("shaders/color_correction_frag.glsl", GL_FRAGMENT_SHADER);
		colorCorrectionShader->Link();

		glm::vec3 lightPos = glm::vec3(0.0f, 0.0f, 2.0f);
		glm::vec3 lightCol = glm::vec3(0.9f, 0.85f, 0.5f);
		float     lightAmbientPow = 0.05f;
		float     lightSpecularPow = 0.0f;
		glm::vec3 ambientCol = glm::vec3(1.0f);
		float     ambientPow = 0.1f;
		float     lightLinearFalloff = 0.09f;
		float     lightQuadraticFalloff = 0.032f;

		float colorSlider = 0.0f;
		float colorOption = 0.0f;

		// These are our application / scene level uniforms that don't necessarily update
		// every frame
		shader->SetUniform("u_LightPos", lightPos);
		shader->SetUniform("u_LightCol", lightCol);
		shader->SetUniform("u_AmbientLightStrength", lightAmbientPow);
		shader->SetUniform("u_SpecularLightStrength", lightSpecularPow);
		shader->SetUniform("u_AmbientCol", ambientCol);
		shader->SetUniform("u_AmbientStrength", ambientPow);
		shader->SetUniform("u_LightAttenuationConstant", 1.0f);
		shader->SetUniform("u_LightAttenuationLinear", lightLinearFalloff);
		shader->SetUniform("u_LightAttenuationQuadratic", lightQuadraticFalloff);

		// We'll add some ImGui controls to control our shader
		BackendHandler::imGuiCallbacks.push_back([&]() {
			if (ImGui::CollapsingHeader("Scene Level Lighting Settings"))
			{
				if (ImGui::ColorPicker3("Ambient Color", glm::value_ptr(ambientCol))) {
					shader->SetUniform("u_AmbientCol", ambientCol);
				}
				if (ImGui::SliderFloat("Fixed Ambient Power", &ambientPow, 0.01f, 1.0f)) {
					shader->SetUniform("u_AmbientStrength", ambientPow);
				}
			}
			if (ImGui::CollapsingHeader("Light Level Lighting Settings"))
			{
				if (ImGui::DragFloat3("Light Pos", glm::value_ptr(lightPos), 0.01f, -10.0f, 10.0f)) {
					shader->SetUniform("u_LightPos", lightPos);
				}
				if (ImGui::ColorPicker3("Light Col", glm::value_ptr(lightCol))) {
					shader->SetUniform("u_LightCol", lightCol);
				}
				if (ImGui::SliderFloat("Light Ambient Power", &lightAmbientPow, 0.0f, 1.0f)) {
					shader->SetUniform("u_AmbientLightStrength", lightAmbientPow);
				}
				if (ImGui::SliderFloat("Light Specular Power", &lightSpecularPow, 0.0f, 1.0f)) {
					shader->SetUniform("u_SpecularLightStrength", lightSpecularPow);
				}
				if (ImGui::DragFloat("Light Linear Falloff", &lightLinearFalloff, 0.01f, 0.0f, 1.0f)) {
					shader->SetUniform("u_LightAttenuationLinear", lightLinearFalloff);
				}
				if (ImGui::DragFloat("Light Quadratic Falloff", &lightQuadraticFalloff, 0.01f, 0.0f, 1.0f)) {
					shader->SetUniform("u_LightAttenuationQuadratic", lightQuadraticFalloff);
				}
			}

			if (ImGui::CollapsingHeader("Color Correction Settings"))
			{
				ImGui::Text("0 for normal, 1 for cool, 2 for warm, and 3 for custom");

				if (ImGui::SliderFloat("Color Correction", &colorSlider, 0.0f, 3.0f))
				{
					colorOption = colorSlider;
				}
			}

			auto name = controllables[selectedVao].get<GameObjectTag>().Name;
			ImGui::Text(name.c_str());
			auto behaviour = BehaviourBinding::Get<SimpleMoveBehaviour>(controllables[selectedVao]);
			ImGui::Checkbox("Relative Rotation", &behaviour->Relative);

			ImGui::Text("Q/E -> Yaw\nLeft/Right -> Roll\nUp/Down -> Pitch\nY -> Toggle Mode");
		
			minFps = FLT_MAX;
			maxFps = 0;
			avgFps = 0;
			for (int ix = 0; ix < 128; ix++) {
				if (fpsBuffer[ix] < minFps) { minFps = fpsBuffer[ix]; }
				if (fpsBuffer[ix] > maxFps) { maxFps = fpsBuffer[ix]; }
				avgFps += fpsBuffer[ix];
			}
			ImGui::PlotLines("FPS", fpsBuffer, 128);
			ImGui::Text("MIN: %f MAX: %f AVG: %f", minFps, maxFps, avgFps / 128.0f);
			});

		#pragma endregion 

		// GL states
		glEnable(GL_DEPTH_TEST);
		glEnable(GL_CULL_FACE);
		glDepthFunc(GL_LEQUAL); // New 

		#pragma region TEXTURE LOADING

		// Load some textures from files
		Texture2D::sptr diffuse = Texture2D::LoadFromFile("images/Stone_001_Diffuse.png");
		Texture2D::sptr diffuse2 = Texture2D::LoadFromFile("images/box.bmp");
		Texture2D::sptr specular = Texture2D::LoadFromFile("images/Stone_001_Specular.png");
		Texture2D::sptr reflectivity = Texture2D::LoadFromFile("images/box-reflections.bmp");
		//LUT3D colorCube("cubes/CustomCorrection.cube");
		LUT3D defaultCube("cubes/Neutral-512.cube"); //default colours
		//color cubes
		LUT3D coolCube("cubes/cool_lut.cube");
		LUT3D warmCube("cubes/warm_lut.cube");
		LUT3D customCube("cubes/custom_lut.cube");

		// Load the cube map
		//TextureCubeMap::sptr environmentMap = TextureCubeMap::LoadFromImages("images/cubemaps/skybox/sample.jpg");
		TextureCubeMap::sptr environmentMap = TextureCubeMap::LoadFromImages("images/cubemaps/skybox/ocean.jpg"); 

		// Creating an empty texture
		Texture2DDescription desc = Texture2DDescription();  
		desc.Width = 1;
		desc.Height = 1;
		desc.Format = InternalFormat::RGB8;
		Texture2D::sptr texture2 = Texture2D::Create(desc);
		// Clear it with a white colour
		texture2->Clear();

		#pragma endregion

		///////////////////////////////////// Scene Generation //////////////////////////////////////////////////
		#pragma region Scene Generation
		
		// We need to tell our scene system what extra component types we want to support
		GameScene::RegisterComponentType<RendererComponent>();
		GameScene::RegisterComponentType<BehaviourBinding>();
		GameScene::RegisterComponentType<Camera>();

		// Create a scene, and set it to be the active scene in the application
		GameScene::sptr scene = GameScene::Create("test");
		Application::Instance().ActiveScene = scene;

		// We can create a group ahead of time to make iterating on the group faster
		entt::basic_group<entt::entity, entt::exclude_t<>, entt::get_t<Transform>, RendererComponent> renderGroup =
			scene->Registry().group<RendererComponent>(entt::get_t<Transform>());

		// Create a material and set some properties for it
		ShaderMaterial::sptr material0 = ShaderMaterial::Create();  
		material0->Shader = shader;
		material0->Set("s_Diffuse", diffuse);
		material0->Set("s_Diffuse2", diffuse2);
		material0->Set("s_Specular", specular);
		material0->Set("u_Shininess", 8.0f);
		material0->Set("u_TextureMix", 0.5f); 

		// Load a second material for our reflective material!
		Shader::sptr reflectiveShader = Shader::Create();
		reflectiveShader->LoadShaderPartFromFile("shaders/vertex_shader.glsl", GL_VERTEX_SHADER);
		reflectiveShader->LoadShaderPartFromFile("shaders/frag_reflection.frag.glsl", GL_FRAGMENT_SHADER);
		reflectiveShader->Link();

		Shader::sptr reflective = Shader::Create();
		reflective->LoadShaderPartFromFile("shaders/vertex_shader.glsl", GL_VERTEX_SHADER);
		reflective->LoadShaderPartFromFile("shaders/frag_blinn_phong_reflection.glsl", GL_FRAGMENT_SHADER);
		reflective->Link();
		
		// 
		ShaderMaterial::sptr material1 = ShaderMaterial::Create(); 
		material1->Shader = reflective;
		material1->Set("s_Diffuse", diffuse);
		material1->Set("s_Diffuse2", diffuse2);
		material1->Set("s_Specular", specular);
		material1->Set("s_Reflectivity", reflectivity); 
		material1->Set("s_Environment", environmentMap); 
		material1->Set("u_LightPos", lightPos);
		material1->Set("u_LightCol", lightCol);
		material1->Set("u_AmbientLightStrength", lightAmbientPow); 
		material1->Set("u_SpecularLightStrength", lightSpecularPow); 
		material1->Set("u_AmbientCol", ambientCol);
		material1->Set("u_AmbientStrength", ambientPow);
		material1->Set("u_LightAttenuationConstant", 1.0f);
		material1->Set("u_LightAttenuationLinear", lightLinearFalloff);
		material1->Set("u_LightAttenuationQuadratic", lightQuadraticFalloff);
		material1->Set("u_Shininess", 8.0f);
		material1->Set("u_TextureMix", 0.5f);
		material1->Set("u_EnvironmentRotation", glm::mat3(glm::rotate(glm::mat4(1.0f), glm::radians(90.0f), glm::vec3(1, 0, 0))));
		
		ShaderMaterial::sptr reflectiveMat = ShaderMaterial::Create();
		reflectiveMat->Shader = reflectiveShader;
		reflectiveMat->Set("s_Environment", environmentMap);
		reflectiveMat->Set("u_EnvironmentRotation", glm::mat3(glm::rotate(glm::mat4(1.0f), glm::radians(90.0f), glm::vec3(1, 0, 0))));

		//GameObject sceneObj = scene->CreateEntity("scene_geo"); 
		//{
		//	VertexArrayObject::sptr sceneVao = NotObjLoader::LoadFromFile("Sample.notobj");
		//	sceneObj.emplace<RendererComponent>().SetMesh(sceneVao).SetMaterial(material1);
		//	sceneObj.get<Transform>().SetLocalPosition(0.0f, 0.0f, 0.0f);
		//}

		//GameObject wall1 = scene->CreateEntity("one_of_walls");
		//{
		//	VertexArrayObject::sptr vao = ObjLoader::LoadFromFile("plane.obj");
		//	wall1.emplace<RendererComponent>().SetMesh(vao).SetMaterial(material0);
		//	wall1.get<Transform>().SetLocalPosition(-5.0f, 0.0f, 0.0f);
		//	wall1.get<Transform>().SetLocalRotation(0.0f, 90.0f, 0.0f);
		//	BehaviourBinding::BindDisabled<SimpleMoveBehaviour>(wall1);
		//}

		
		GameObject obj2 = scene->CreateEntity("monkey_quads");
		{
			VertexArrayObject::sptr vao = ObjLoader::LoadFromFile("models/monkey_quads.obj");
			obj2.emplace<RendererComponent>().SetMesh(vao).SetMaterial(material0);
			obj2.get<Transform>().SetLocalPosition(0.0f, 0.0f, 1000.0f);
			BehaviourBinding::BindDisabled<SimpleMoveBehaviour>(obj2);
		}

		GameObject obj3 = scene->CreateEntity("paddle1");
		{
			VertexArrayObject::sptr vao = ObjLoader::LoadFromFile("models/red_paddle.obj");
			obj3.emplace<RendererComponent>().SetMesh(vao).SetMaterial(reflectiveMat);
			obj3.get<Transform>().SetLocalPosition(0.0f, 0.0f, 0.0f);
			obj3.get<Transform>().SetLocalRotation(90.0f, 0.0f, 0.0f);
			BehaviourBinding::BindDisabled<SimpleMoveBehaviour>(obj3);
		}


		GameObject obj5 = scene->CreateEntity("cube");
		{
			MeshBuilder<VertexPosNormTexCol> builder = MeshBuilder<VertexPosNormTexCol>();
			MeshFactory::AddCube(builder, glm::vec3(0.0f), glm::vec3(1.0f), glm::vec3(0.0f));
			VertexArrayObject::sptr vao = builder.Bake();
			
			obj5.emplace<RendererComponent>().SetMesh(vao).SetMaterial(reflectiveMat);
			obj5.get<Transform>().SetLocalPosition(-4.0f, 0.0f, 2000.0f);
			BehaviourBinding::BindDisabled<SimpleMoveBehaviour>(obj5);
		}
		

		//GameObject obj4 = scene->CreateEntity("moving_box");
		//{
		//	// Build a mesh
		//	MeshBuilder<VertexPosNormTexCol> builder = MeshBuilder<VertexPosNormTexCol>();
		//	MeshFactory::AddCube(builder, glm::vec3(0.0f), glm::vec3(1.0f), glm::vec3(0.0f), glm::vec4(1.0f, 0.5f, 0.5f, 1.0f));
		//	VertexArrayObject::sptr vao = builder.Bake();
		//	
		//	obj4.emplace<RendererComponent>().SetMesh(vao).SetMaterial(material0);
		//	obj4.get<Transform>().SetLocalPosition(-2.0f, 0.0f, 1.0f);

		//	// Bind returns a smart pointer to the behaviour that was added
		//	auto pathing = BehaviourBinding::Bind<FollowPathBehaviour>(obj4);
		//	// Set up a path for the object to follow
		//	pathing->Points.push_back({ -4.0f, -4.0f, 0.0f });
		//	pathing->Points.push_back({ 4.0f, -4.0f, 0.0f });
		//	pathing->Points.push_back({ 4.0f,  4.0f, 0.0f });
		//	pathing->Points.push_back({ -4.0f,  4.0f, 0.0f });
		//	pathing->Speed = 2.0f;
		//}

		//GameObject obj6 = scene->CreateEntity("johnny");
		//{
		//	VertexArrayObject::sptr vao = ObjLoader::LoadFromFile("models/JohnnyEmote.obj");
		//	obj6.emplace<RendererComponent>().SetMesh(vao).SetMaterial(reflectiveMat);
		//	obj6.get<Transform>().SetLocalPosition(0.0f, 0.0f, 3.0f);
		//	obj6.get<Transform>().SetLocalRotation(90.0f, 0.0f, 0.0f);
	

		//	obj6.get<Transform>().SetParent(obj4);
		//	
		//	auto pathing = BehaviourBinding::Bind<FollowPathBehaviour>(obj6);
		//	// Set up a path for the object to follow
		//	pathing->Points.push_back({ 0.0f, -2.5f, 2.5f });
		//	pathing->Points.push_back({ 0.0f, -2.5f, 2.5f });
		//	pathing->Speed = 2.0f;
		//}

		GameObject obj7 = scene->CreateEntity("paddle2");
		{
			VertexArrayObject::sptr vao = ObjLoader::LoadFromFile("models/blue_paddle.obj");
			obj7.emplace<RendererComponent>().SetMesh(vao).SetMaterial(reflectiveMat);
			obj7.get<Transform>().SetLocalPosition(2.0f, 0.0f, 0.0f);
			obj7.get<Transform>().SetLocalRotation(90.0f, 0.0f, 0.0f);
			BehaviourBinding::BindDisabled<SimpleMoveBehaviour>(obj7);
		}
	
		GameObject obj8 = scene->CreateEntity("paddle2");
		{
			VertexArrayObject::sptr vao = ObjLoader::LoadFromFile("models/dagger.obj");
			obj8.emplace<RendererComponent>().SetMesh(vao).SetMaterial(reflectiveMat);
			obj8.get<Transform>().SetLocalPosition(4.0f, 0.0f, 2.0f);
			obj8.get<Transform>().SetLocalRotation(0.0f, -90.0f, 0.0f);
			BehaviourBinding::BindDisabled<SimpleMoveBehaviour>(obj8);
		}

		GameObject obj9 = scene->CreateEntity("wall1");
		{
			VertexArrayObject::sptr vao = ObjLoader::LoadFromFile("models/plane.obj");
			obj9.emplace<RendererComponent>().SetMesh(vao).SetMaterial(material0 );
			obj9.get<Transform>().SetLocalPosition(-5.0f, 0.0f, 2.0f);
			obj9.get<Transform>().SetLocalRotation(0.0f, 90.0f, 0.0f);
			obj9.get<Transform>().SetLocalScale(5.0f, 5.0f, 5.0f);
			BehaviourBinding::BindDisabled<SimpleMoveBehaviour>(obj9);
		}

		GameObject obj10 = scene->CreateEntity("wall2");
		{
			VertexArrayObject::sptr vao = ObjLoader::LoadFromFile("models/plane.obj");
			obj10.emplace<RendererComponent>().SetMesh(vao).SetMaterial(material0);
			obj10.get<Transform>().SetLocalPosition(0.0f, -5.0f, 2.0f);
			obj10.get<Transform>().SetLocalRotation(0.0f, 90.0f, 90.0f);
			obj10.get<Transform>().SetLocalScale(5.0f, 5.0f, 5.0f);
			BehaviourBinding::BindDisabled<SimpleMoveBehaviour>(obj10);
		}

		GameObject obj11 = scene->CreateEntity("floor");
		{
			VertexArrayObject::sptr vao = ObjLoader::LoadFromFile("models/plane.obj");
			obj11.emplace<RendererComponent>().SetMesh(vao).SetMaterial(material0);
			obj11.get<Transform>().SetLocalPosition(0.0f, 0.0f, 0.0f);
			obj11.get<Transform>().SetLocalRotation(0.0f, 0.0f, 0.0f);
			obj11.get<Transform>().SetLocalScale(5.0f, 5.0f, 5.0f);
			BehaviourBinding::BindDisabled<SimpleMoveBehaviour>(obj11);
		}


		// Create an object to be our camera
		GameObject cameraObject = scene->CreateEntity("Camera");
		{
			cameraObject.get<Transform>().SetLocalPosition(0, 3, 3).LookAt(glm::vec3(0, 0, 0));

			// We'll make our camera a component of the camera object
			Camera& camera = cameraObject.emplace<Camera>();// Camera::Create();
			camera.SetPosition(glm::vec3(0, 3, 3));
			camera.SetUp(glm::vec3(0, 0, 1));
			camera.LookAt(glm::vec3(0));
			camera.SetFovDegrees(90.0f); // Set an initial FOV
			camera.SetOrthoHeight(3.0f);
			BehaviourBinding::Bind<CameraControlBehaviour>(cameraObject);
		}

		int width, height;
		glfwGetWindowSize(BackendHandler::window, &width, &height);

		Framebuffer* colorCorrect;
		GameObject colorCorrectionObj = scene->CreateEntity("Color Correct");
		{
			colorCorrect = &colorCorrectionObj.emplace<Framebuffer>();
			colorCorrect->AddColorTarget(GL_RGBA8);
			colorCorrect->AddDepthTarget();
			colorCorrect->Init(width, height);
		}

		#pragma endregion 
		//////////////////////////////////////////////////////////////////////////////////////////

		/////////////////////////////////// SKYBOX ///////////////////////////////////////////////
		{
			// Load our shaders
			Shader::sptr skybox = std::make_shared<Shader>();
			skybox->LoadShaderPartFromFile("shaders/skybox-shader.vert.glsl", GL_VERTEX_SHADER);
			skybox->LoadShaderPartFromFile("shaders/skybox-shader.frag.glsl", GL_FRAGMENT_SHADER);
			skybox->Link();

			ShaderMaterial::sptr skyboxMat = ShaderMaterial::Create();
			skyboxMat->Shader = skybox;  
			skyboxMat->Set("s_Environment", environmentMap);
			skyboxMat->Set("u_EnvironmentRotation", glm::mat3(glm::rotate(glm::mat4(1.0f), glm::radians(90.0f), glm::vec3(1, 0, 0))));
			skyboxMat->RenderLayer = 100;

			MeshBuilder<VertexPosNormTexCol> mesh;
			MeshFactory::AddIcoSphere(mesh, glm::vec3(0.0f), 1.0f);
			MeshFactory::InvertFaces(mesh);
			VertexArrayObject::sptr meshVao = mesh.Bake();
			
			GameObject skyboxObj = scene->CreateEntity("skybox");  
			skyboxObj.get<Transform>().SetLocalPosition(0.0f, 0.0f, 0.0f);
			skyboxObj.get_or_emplace<RendererComponent>().SetMesh(meshVao).SetMaterial(skyboxMat);
		}
		////////////////////////////////////////////////////////////////////////////////////////


		// We'll use a vector to store all our key press events for now (this should probably be a behaviour eventually)
		std::vector<KeyPressWatcher> keyToggles;
		{
			// This is an example of a key press handling helper. Look at InputHelpers.h an .cpp to see
			// how this is implemented. Note that the ampersand here is capturing the variables within
			// the scope. If you wanted to do some method on the class, your best bet would be to give it a method and
			// use std::bind
			keyToggles.emplace_back(GLFW_KEY_T, [&]() { cameraObject.get<Camera>().ToggleOrtho(); });

			controllables.push_back(obj2);
			controllables.push_back(obj3);

			keyToggles.emplace_back(GLFW_KEY_KP_ADD, [&]() {
				BehaviourBinding::Get<SimpleMoveBehaviour>(controllables[selectedVao])->Enabled = false;
				selectedVao++;
				if (selectedVao >= controllables.size())
					selectedVao = 0;
				BehaviourBinding::Get<SimpleMoveBehaviour>(controllables[selectedVao])->Enabled = true;
				});
			keyToggles.emplace_back(GLFW_KEY_KP_SUBTRACT, [&]() {
				BehaviourBinding::Get<SimpleMoveBehaviour>(controllables[selectedVao])->Enabled = false;
				selectedVao--;
				if (selectedVao < 0)
					selectedVao = controllables.size() - 1;
				BehaviourBinding::Get<SimpleMoveBehaviour>(controllables[selectedVao])->Enabled = true;
				});

			keyToggles.emplace_back(GLFW_KEY_Y, [&]() {
				auto behaviour = BehaviourBinding::Get<SimpleMoveBehaviour>(controllables[selectedVao]);
				behaviour->Relative = !behaviour->Relative;
				});
		}

		// Custom Code

		keyToggles.emplace_back(GLFW_KEY_1, [&]() {
			
			lightAmbientPow = 0.0f;
			lightSpecularPow = 0.0f;
			});

		keyToggles.emplace_back(GLFW_KEY_2, [&]() {

			glm::vec3 lightPos = glm::vec3(0.0f, 0.0f, 0.0f);
			glm::vec3 lightCol = glm::vec3(0.0f, 0.0f, 0.0f);
			lightAmbientPow = 0.0f;
			lightSpecularPow = 0.0f;
			glm::vec3 ambientCol = glm::vec3(0.0f);
			ambientPow = 0.0f;
			lightLinearFalloff = 0.0f;
			lightQuadraticFalloff = 0.0f;
			});
		keyToggles.emplace_back(GLFW_KEY_3, [&]() {

			lightAmbientPow = 0.0f;
			lightSpecularPow = 0.0f;


			});
		keyToggles.emplace_back(GLFW_KEY_4, [&]() {

			//glm::value_ptr(ambientCol), 0.00f, 0.0f, 0.0f;

			});
		keyToggles.emplace_back(GLFW_KEY_5, [&]() {

			//glm::value_ptr(ambientCol), 100.00f, 100.0f, 100.0f;

			});

	



		// Initialize our timing instance and grab a reference for our use
		Timing& time = Timing::Instance();
		time.LastFrame = glfwGetTime();

		///// Game loop /////
		while (!glfwWindowShouldClose(BackendHandler::window)) {
			glfwPollEvents();

			// Update the timing
			time.CurrentFrame = glfwGetTime();
			time.DeltaTime = static_cast<float>(time.CurrentFrame - time.LastFrame);

			time.DeltaTime = time.DeltaTime > 1.0f ? 1.0f : time.DeltaTime;

			// Update our FPS tracker data
			fpsBuffer[frameIx] = 1.0f / time.DeltaTime;
			frameIx++;
			if (frameIx >= 128)
				frameIx = 0;

			// We'll make sure our UI isn't focused before we start handling input for our game
			if (!ImGui::IsAnyWindowFocused()) {
				// We need to poll our key watchers so they can do their logic with the GLFW state
				// Note that since we want to make sure we don't copy our key handlers, we need a const
				// reference!
				for (const KeyPressWatcher& watcher : keyToggles) {
					watcher.Poll(BackendHandler::window);
				}
			}

			// Iterate over all the behaviour binding components
			scene->Registry().view<BehaviourBinding>().each([&](entt::entity entity, BehaviourBinding& binding) {
				// Iterate over all the behaviour scripts attached to the entity, and update them in sequence (if enabled)
				for (const auto& behaviour : binding.Behaviours) {
					if (behaviour->Enabled) {
						behaviour->Update(entt::handle(scene->Registry(), entity));
					}
				}
			});

			// Clear the screen
			colorCorrect->Clear();

			glClearColor(0.08f, 0.17f, 0.31f, 1.0f);
			glEnable(GL_DEPTH_TEST);
			glClearDepth(1.0f);
			glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

			// Update all world matrices for this frame
			scene->Registry().view<Transform>().each([](entt::entity entity, Transform& t) {
				t.UpdateWorldMatrix();
			});
			
			// Grab out camera info from the camera object
			Transform& camTransform = cameraObject.get<Transform>();
			glm::mat4 view = glm::inverse(camTransform.LocalTransform());
			glm::mat4 projection = cameraObject.get<Camera>().GetProjection();
			glm::mat4 viewProjection = projection * view;
						
			// Sort the renderers by shader and material, we will go for a minimizing context switches approach here,
			// but you could for instance sort front to back to optimize for fill rate if you have intensive fragment shaders
			renderGroup.sort<RendererComponent>([](const RendererComponent& l, const RendererComponent& r) {
				// Sort by render layer first, higher numbers get drawn last
				if (l.Material->RenderLayer < r.Material->RenderLayer) return true;
				if (l.Material->RenderLayer > r.Material->RenderLayer) return false;

				// Sort by shader pointer next (so materials using the same shader run sequentially where possible)
				if (l.Material->Shader < r.Material->Shader) return true;
				if (l.Material->Shader > r.Material->Shader) return false;

				// Sort by material pointer last (so we can minimize switching between materials)
				if (l.Material < r.Material) return true;
				if (l.Material > r.Material) return false;
				
				return false;
			});

			// Start by assuming no shader or material is applied
			Shader::sptr current = nullptr;
			ShaderMaterial::sptr currentMat = nullptr;
			
			colorCorrect->Bind();

			// Iterate over the render group components and draw them
			renderGroup.each( [&](entt::entity e, RendererComponent& renderer, Transform& transform) {
				// If the shader has changed, set up it's uniforms
				if (current != renderer.Material->Shader) {
					current = renderer.Material->Shader;
					current->Bind();
					BackendHandler::SetupShaderForFrame(current, view, projection);
				}
				// If the material has changed, apply it
				if (currentMat != renderer.Material) {
					currentMat = renderer.Material;
					currentMat->Apply();
				}
				// Render the mesh
				BackendHandler::RenderVAO(renderer.Material->Shader, renderer.Mesh, viewProjection, transform);
			});

			colorCorrect->Unbind();
			colorCorrectionShader->Bind();

			colorCorrect->BindColorAsTexture(0, 0);

			if (colorOption < 1)
			{
				defaultCube.bind(30);

				colorCorrect->DrawFullscreenQuad();

				defaultCube.unbind(30);
			}
			else if (colorOption < 2)
			{
				coolCube.bind(30);

				colorCorrect->DrawFullscreenQuad();

				coolCube.unbind(30);
			}
			else if (colorOption < 3)
			{
				warmCube.bind(30);

				colorCorrect->DrawFullscreenQuad();

				warmCube.unbind(30);
			}
			else if (colorOption == 3)
			{
				customCube.bind(30);

				colorCorrect->DrawFullscreenQuad();

				customCube.unbind(30);
			}
			
			colorCorrect->UnbindTexture(0);

			colorCorrectionShader->UnBind();


			// Draw our ImGui content
			BackendHandler::RenderImGui();

			scene->Poll();
			glfwSwapBuffers(BackendHandler::window);
			time.LastFrame = time.CurrentFrame;
		}

		// Nullify scene so that we can release references
		Application::Instance().ActiveScene = nullptr;
		BackendHandler::ShutdownImGui();
	}	

	// Clean up the toolkit logger so we don't leak memory
	Logger::Uninitialize();
	return 0;
}